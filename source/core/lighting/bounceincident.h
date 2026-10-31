#ifndef POVRAY_CORE_LIGHTING_BOUNCEINCIDENT_H
#define POVRAY_CORE_LIGHTING_BOUNCEINCIDENT_H

#include "core/configcore.h"
#include "core/lighting/bounceorders.h"
#include "core/lighting/emitter.h"
#include "core/lighting/progressivephoton.h"
#include "core/material/volumeincident.h"
#include "core/math/randomsequence.h"
#include "core/render/ray.h"
#include "core/render/trace.h"
#include "core/scene/object.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace pov
{

struct BounceIncidentBeam final
{
    Vector3d direction; // Unit propagation direction, from the light to the receiving point; zero from every direction.
    MathColour angularWeight; // Physical incoming radiance integrated over this sample's solid angle.
    double distance = HUGE_VAL; // From the receiving point to where the light was drawn.
};

// Source emitters and the supplied Trace's shadow-media evaluator must read the immutable previous order. A receiver
// with a radius stands for the region about it: a light nearer than that is seen from all of it (Ray::ReceiverRadius).
inline std::vector<BounceIncidentBeam> CollectBounceIncident(
    Trace& trace, const Vector3d& point, const std::vector<LightSource*>& sources,
    unsigned int samplesPerSource, std::uint64_t key, TraceTicket& ticket,
    const Intersection* receiverHit = nullptr, const Vector3d* receiverNormal = nullptr, double receiverRadius = 0.0)
{
    std::vector<BounceIncidentBeam> beams;
    if (samplesPerSource == 0)
        return beams;
    RayInteriorVector interiors;
    trace.FindContainingInteriors(point, interiors, ticket.preparedSetId);
    ConstObjectPtr receiver = receiverHit ? receiverHit->Object : nullptr;
    Interior* receiverInterior = receiver ? receiver->interior.get() : nullptr;
    if (receiver && receiverNormal)
    {
        RayInteriorVector filtered;
        for (Interior* interior : interiors)
            if (interior != receiverInterior)
                filtered.push_back(interior);
        interiors.clear();
        for (Interior* interior : filtered)
            interiors.push_back(interior);
    }
    for (std::size_t source = 0; source < sources.size(); ++source)
    {
        if (sources[source] == nullptr)
            continue;
        const LightSource& light = *sources[source];
        for (unsigned int sample = 0; sample < samplesPerSource; ++sample)
        {
            const std::uint64_t sampleKey = BounceOrderSeed(key, 0, sample, source);
            Ray ray(ticket, Ray::OtherRay, true);
            ray.Origin = point;
            ray.Direction = receiver && receiverNormal ? -*receiverNormal : Vector3d(0.0, 0.0, 1.0);
            ray.SetKey(sampleKey);
            ticket.keyRead = false;
            ray.WatchKey();
            ray.AppendInteriors(interiors);
            if (receiver && receiverNormal)
                ray.SetSurfaceOrigin(receiver, point, *receiverNormal);
            ray.SetExactShadowWindow();
            ray.SetReceiverRadius(receiverRadius);
            MathColour incoming;
            double depth = 0.0;
            if (light.emitter)
            {
                const EmitterDraw draw(Draw(sampleKey, kDrawEmitter, 0), Draw(sampleKey, kDrawEmitter, 1),
                                       Draw(sampleKey, kDrawEmitter, 2), Draw(sampleKey, kDrawEmitter, 3),
                                       DeriveKey(sampleKey, kDrawEmitter, UINT64_C(0x706f736974696f6e)));
                if (!trace.SampleEmitterConnection(light, point, ray, depth, incoming, draw, true,
                                                   receiverNormal, receiverInterior))
                    continue;
            }
            else
            {
                if (trace.TestShadow(light, depth, ray, point, incoming))
                    incoming.Clear();
                else
                    incoming *= M_PI;
            }
            // An emitter draws from the sample key itself; any other draw that read no ray key stands for every sample.
            const bool repeated = !light.emitter && !ticket.keyRead;
            const double share = repeated ? 1.0 : 1.0 / samplesPerSource;
            bool positive = false;
            for (int channel = 0; channel < MathColour::channels; ++channel)
            {
                if (!std::isfinite(incoming[channel]) || incoming[channel] < 0.0)
                    throw std::domain_error("bounce incident light must be finite and nonnegative");
                positive = positive || incoming[channel] > 0.0;
            }
            const double length = ray.Direction.length();
            if (positive && (!std::isfinite(length) || length <= 0.0))
                throw std::domain_error("bounce incident direction must be finite and nonzero");
            // A light drawn nearer than the receiver's radius lights its region from around it, that share from every
            // side; an emitter's draws already spread over its own extent.
            const double directed = !light.emitter && depth < receiverRadius ? depth / receiverRadius : 1.0;
            const double distance = depth > 0.0 ? depth : HUGE_VAL;
            if (positive && directed > 0.0)
                beams.push_back(BounceIncidentBeam{-ray.Direction / length, incoming * (share * directed), distance});
            if (positive && directed < 1.0)
                beams.push_back(BounceIncidentBeam{Vector3d(0.0), incoming * (share * (1.0 - directed)), distance});
            if (repeated)
                break;
        }
    }
    return beams;
}

inline std::vector<BounceIncidentBeam> CollectBounceIncident(
    Trace& trace, const Vector3d& point, const std::vector<LightSource*>& sources,
    unsigned int samplesPerSource, std::uint64_t key,
    const Intersection* receiverHit = nullptr, const Vector3d* receiverNormal = nullptr, double receiverRadius = 0.0)
{
    TraceTicket ticket(256, 0.0);
    return CollectBounceIncident(trace, point, sources, samplesPerSource, key, ticket, receiverHit, receiverNormal,
                                 receiverRadius);
}

inline std::vector<BounceIncidentBeam> CollectBounceIncident(
    Trace& trace, const Vector3d& point, const std::vector<LightSource*>& sources,
    unsigned int samplesPerSource, std::uint64_t key, ConstObjectPtr receiver, const Vector3d& normal)
{
    Intersection hit;
    hit.Object = const_cast<ObjectPtr>(receiver);
    hit.IPoint = point;
    return CollectBounceIncident(trace, point, sources, samplesPerSource, key, &hit, &normal);
}


// Draws per light at each volume bounce node, as a multiple of the bouncing light's samples.
const unsigned int kVolumeDrawsPerSample = 2;

/// Where a volume bounce node gathers: its lights, the bouncing light's samples, and the containers its medium fills
/// (draw points outside them receive nothing).
struct VolumeGather final
{
    std::vector<const LightSource*> lights;
    unsigned int samples = 1;
    std::vector<ConstObjectPtr> receivers;
};

namespace volume_gather_detail
{
/// The part [near, far] of the ray from `from` along unit `direction` inside the box `low`..`high`, from no nearer than 0.
inline bool BoxChord(const Vector3d& from, const Vector3d& direction, const Vector3d& low, const Vector3d& high,
                     double& near, double& far)
{
    near = 0.0;
    far = HUGE_VAL;
    for (int axis = 0; axis < 3; axis++)
    {
        if (direction[axis] == 0.0)
        {
            if ((from[axis] < low[axis]) || (from[axis] > high[axis]))
                return false;
            continue;
        }
        double a = (low[axis] - from[axis]) / direction[axis], b = (high[axis] - from[axis]) / direction[axis];
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
    }
    return far > near;
}

/// Directions from a point light toward a cell: the cone about its bounding sphere, or every direction from inside it.
struct LightCone final
{
    Vector3d axis, u, v;
    double cosMax = -1.0, solidAngle = 4.0 * M_PI;
    LightCone(const Vector3d& light, const Vector3d& centre, const Vector3d& cell)
    {
        const Vector3d offset = centre - light;
        const double distance = offset.length(), radius = 0.5 * cell.length();
        axis = distance > 0.0 ? offset / distance : Vector3d(0.0, 0.0, 1.0);
        if (distance > radius)
        {
            cosMax = std::sqrt(std::max(0.0, 1.0 - (radius / distance) * (radius / distance)));
            solidAngle = 2.0 * M_PI * (1.0 - cosMax);
        }
        u = cross(axis, std::fabs(axis[X]) < 0.9 ? Vector3d(1.0, 0.0, 0.0) : Vector3d(0.0, 1.0, 0.0)).normalized();
        v = cross(axis, u);
    }
    Vector3d Direction(double s, double t) const
    {
        const double cosine = 1.0 - s * (1.0 - cosMax), sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
        return axis * cosine + u * (sine * std::cos(2.0 * M_PI * t)) + v * (sine * std::sin(2.0 * M_PI * t));
    }
};
}

/// The light `gather` sends a volume node's cell as directional lobes; points outside the receivers or in an opaque solid
/// are left out of the mean (see doc/html/r3_12.html).
inline VolumeIncident GatherVolumeIncident(Trace& trace, const Vector3d& centre, const Vector3d& cell, const VolumeGather& gather,
                                           std::uint64_t key, TraceTicket& ticket, std::vector<IncidentDraw>& draws,
                                           std::vector<int>& scratch)
{
    using namespace volume_gather_detail;
    draws.clear();
    RayInteriorVector interiors;
    const Vector3d low = centre - cell * 0.5, high = centre + cell * 0.5;
    const double volume = cell[X] * cell[Y] * cell[Z];
    bool received = false;
    for (const LightSource* light : gather.lights)
    {
        const unsigned int count = gather.samples * kVolumeDrawsPerSample;
        const std::uint64_t lightKey = DeriveKey(key, kDrawAreaLight,
                                                 std::uint64_t(light->index) | (light->lightGroupLight ? (std::uint64_t(1) << 63) : 0));
        const PhotonSobol sequence(lightKey), spread(DeriveKey(lightKey, kDrawPosition, 0)),
                          chords(DeriveKey(lightKey, kDrawPosition, 1));
        const bool point = !light->emitter;
        const LightCone cone(light->Center, centre, cell);
        const std::size_t first = draws.size();
        double total = 0.0;
        // Half of a point light's draws lie along chords from it, so the inverse square near it stays bounded.
        for (unsigned int j = 0; j < count; ++j)
        {
            Vector3d at = centre;
            const bool chord = point && (j % 2 == 1);
            const unsigned int index = point ? j / 2 : j;
            if (chord)
            {
                double near, far;
                const Vector3d direction = cone.Direction(chords(index, 0), chords(index, 1));
                if (!BoxChord(light->Center, direction, low, high, near, far))
                    continue;
                at = light->Center + direction * (near + chords(index, 2) * (far - near));
            }
            else
                for (int axis = 0; axis < 3; axis++)
                    at[axis] += (spread(index, axis) - 0.5) * cell[axis];
            // The mixture of uniform and chord densities, relative to uniform, weights each draw toward the cell mean.
            double weight = 1.0;
            if (point)
            {
                const Vector3d offset = at - light->Center;
                const double r = offset.length();
                double near, far, chordDensity = 0.0;
                if ((r > 0.0) && BoxChord(light->Center, offset / r, low, high, near, far) &&
                    (dot(offset / r, cone.axis) >= cone.cosMax))
                    chordDensity = volume / (cone.solidAngle * (far - near) * r * r);
                weight = 1.0 / (0.5 + 0.5 * chordDensity);
            }
            if (!gather.receivers.empty() && !trace.InsideAny(at, gather.receivers))
                continue;
            Ray ray(ticket, Ray::OtherRay, true);
            ray.Origin = at;
            ray.Direction = Vector3d(0.0, 0.0, 1.0);
            ray.SetKey(DeriveKey(lightKey, kDrawShadow, j));
            interiors.clear();
            trace.FindContainingInteriors(at, interiors, ticket.preparedSetId);
            ray.AppendInteriors(interiors);
            ray.SetExactShadowWindow();
            MathColour incoming;
            double depth = 0.0;
            bool lit;
            if (light->emitter)
            {
                const EmitterDraw draw(EmitterDraw::JitterSelection(sequence(j, 0), count, Draw(lightKey, kDrawEmitter, j)),
                                       sequence(j, 1), sequence(j, 2), sequence(j, 3),
                                       DeriveKey(DeriveKey(lightKey, kDrawEmitter, j), kDrawEmitter, UINT64_C(0x706f736974696f6e)));
                lit = trace.SampleEmitterConnection(*light, at, ray, depth, incoming, draw);
            }
            else
            {
                lit = !trace.TestShadow(*light, depth, ray, at, incoming);
                incoming *= M_PI;
            }
            const double length = ray.Direction.length();
            if (!lit && (depth > 0.0) && (length > 0.0) && std::isfinite(length))
            {
                Ray probe(ticket, Ray::OtherRay, true);
                probe.Origin = at;
                probe.Direction = ray.Direction / length;
                if (trace.LeavesOpaqueSolid(probe, depth))
                    continue;
            }
            total += weight;
            received = true;
            if (!lit)
                continue;
            for (int channel = 0; channel < MathColour::channels; ++channel)
                if (!std::isfinite(incoming[channel]))
                    throw std::domain_error("Light arriving in a volume must be finite");
            if (incoming.IsZero())
                continue;
            if (!(length > 0.0) || !std::isfinite(length))
                throw std::domain_error("Light arriving in a volume needs a finite direction");
            draws.push_back(IncidentDraw{ray.Direction / length, incoming * weight});
        }
        for (std::size_t d = first; d < draws.size(); ++d)
            draws[d].light /= total;
    }
    VolumeIncident result = FitIncidentLobes(draws, scratch);
    result.valid = received;
    return result;
}

}
#endif
