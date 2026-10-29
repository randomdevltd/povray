#ifndef POVRAY_CORE_LIGHTING_BOUNCEINCIDENT_H
#define POVRAY_CORE_LIGHTING_BOUNCEINCIDENT_H

#include "core/configcore.h"
#include "core/lighting/bounceorders.h"
#include "core/lighting/emitter.h"
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
    Vector3d direction; // Unit propagation direction, from the light to the receiving point.
    MathColour angularWeight; // Physical incoming radiance integrated over this sample's solid angle.
};

// Source emitters and the supplied Trace's shadow-media evaluator must read the immutable previous order.
inline std::vector<BounceIncidentBeam> CollectBounceIncident(
    Trace& trace, const Vector3d& point, const std::vector<LightSource*>& sources,
    unsigned int samplesPerSource, std::uint64_t key, TraceTicket& ticket,
    const Intersection* receiverHit = nullptr, const Vector3d* receiverNormal = nullptr)
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
            bool keyRead = false;
            ray.WatchKey(&keyRead);
            ray.AppendInteriors(interiors);
            if (receiver && receiverNormal)
                ray.SetSurfaceOrigin(receiver, point, *receiverNormal);
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
            const bool repeated = !light.emitter && !keyRead;
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
            if (positive)
                beams.push_back(BounceIncidentBeam{-ray.Direction / length, incoming * share});
            if (repeated)
                break;
        }
    }
    return beams;
}

inline std::vector<BounceIncidentBeam> CollectBounceIncident(
    Trace& trace, const Vector3d& point, const std::vector<LightSource*>& sources,
    unsigned int samplesPerSource, std::uint64_t key,
    const Intersection* receiverHit = nullptr, const Vector3d* receiverNormal = nullptr)
{
    TraceTicket ticket(256, 0.0);
    return CollectBounceIncident(trace, point, sources, samplesPerSource, key, ticket, receiverHit, receiverNormal);
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

}
#endif
