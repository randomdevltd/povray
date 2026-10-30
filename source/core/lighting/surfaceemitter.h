#ifndef POVRAY_CORE_SURFACEEMITTER_H
#define POVRAY_CORE_SURFACEEMITTER_H

#include "core/configcore.h"
#include "core/lighting/emitter.h"
#include "core/lighting/surfacegeometry.h"
#include "core/material/preparedsurface.h"
#include "core/math/randomsequence.h"
#include "core/render/trace.h"
#include <mutex>
#include <set>

namespace pov
{

class GeometrySurfaceEmitter final : public Emitter
{
public:
    GeometrySurfaceEmitter(ObjectPtr owner, std::shared_ptr<const MediaLight> settings) :
        owner(owner), settings(std::move(settings)), geometry(owner), centre(geometry.Centre()), radius(geometry.Radius()),
        guide(Vector3d(owner->BBox.lowerLeft), Vector3d(owner->BBox.size))
    {
        if (!geometry.IsValid())
            throw POV_EXCEPTION_STRING("A surface light requires finite geometry with nonzero surface area.");
    }
    bool IsSurface() const override { return true; }
    void AccumulateSurfaceGuide(SurfaceAngularGuide& target) const override { target.AddScaled(guide, scale); }
    MathColour SurfaceRadianceAt(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing,
                                 TraceThreadData* td) const override
    {
        if (!field)
            throw POV_EXCEPTION_STRING("Surface light field was not prepared before tracing.");
        return dot(normal, outgoing) > 0.0 ? field->Emission(hit, normal, td) * scale : MathColour();
    }
    const MathColour& Intensity() const override { return intensity; }
    const Vector3d& Centre() const override { return centre; }
    double Radius() const override { return radius; }
    double Reach(const Vector3d& from) const override { return BoxReach(from, geometry.Lower(), geometry.Size()); }

    void ResolveGeometry() const override { geometry.ResolveDeferred(); }

    void PrepareAxis(std::shared_ptr<const PreparedSourceDomain>, TraceThreadData* td,
                     const std::function<void()>& cooperate) const override
    {
        std::lock_guard<std::mutex> lock(prepareMutex);
        if (field) return;
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        const std::shared_ptr<const PreparedSurfaceInput> prepared =
            PreparedSurfaceInput::Prepare(owner, settings->pigment, settings->useEmission, td);
        std::set<const TEXTURE*> validated;
        SurfaceAngularGuide preparedGuide(Vector3d(owner->BBox.lowerLeft), Vector3d(owner->BBox.size));
        MathColour integral;
        std::vector<SurfaceAngularGuide::ConePoint> points;
        constexpr unsigned count = 16384;
        const bool zero = prepared->IsConstant() && prepared->ConstantColour().IsZero();
        for (unsigned i = 0; !zero && i < count; ++i)
        {
            if (cooperate && (i % 256 == 0)) cooperate();
            SurfaceGeometrySample sample;
            const auto key = DeriveKey(UINT64_C(0x736f757263656172), kDrawEmitter, i);
            if (geometry.SampleArea(ray, td, (i + 0.5) / count, Draw(key, kDrawEmitter, 0),
                                    Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), sample))
            {
                const MathColour power = prepared->Emission(sample.intersection, sample.normal, td, &validated) * sample.areaWeight;
                integral += power;
                preparedGuide.Add(sample.intersection.IPoint, power.Greyscale() / double(count), sample.normal);
                if (power.Greyscale() > 0.0) points.push_back({sample.intersection.IPoint, sample.normal, power.Greyscale()});
            }
        }
        integral /= double(count);
        double preparedScale = settings->brightness;
        if (settings->lumens >= 0.0)
        {
            const double power = M_PI * integral.Greyscale();
            if (settings->lumens > 0.0 && !(power > 0.0))
                throw POV_EXCEPTION_STRING("A surface light stated in lumens requires positive surface emission.");
            preparedScale = settings->lumens == 0.0 ? 0.0 : settings->lumens / power;
        }
        preparedGuide.PrepareCones(std::move(points));
        preparedGuide.Prepare();
        ChooseShares(preparedGuide, *prepared, td, cooperate);
        guide = std::move(preparedGuide);
        scale = preparedScale;
        intensity = integral * preparedScale;
        field = prepared;
    }

    EmitterSample Sample(double s, std::uint64_t key, TraceThreadData* td = nullptr) const override
    {
        if (!field)
            throw POV_EXCEPTION_STRING("Surface light field was not prepared before tracing.");
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        SurfaceGeometrySample surface;
        EmitterSample sample{};
        if (!geometry.SampleArea(ray, td, s, Draw(key, kDrawEmitter, 0), Draw(key, kDrawEmitter, 1),
                                 Draw(key, kDrawEmitter, 2), surface))
            return sample;
        sample.position = surface.intersection.IPoint;
        sample.normal = surface.normal;
        sample.surfaceObject = surface.intersection.Object;
        sample.pdf = 1.0 / surface.areaWeight;
        sample.weight = field->Emission(surface.intersection, surface.normal, td) * (scale * surface.areaWeight);
        return sample;
    }

    bool SampleToward(const Vector3d& from, const EmitterDraw& draw, TraceThreadData* td,
                      EmitterSample& sample) const override
    {
        if (!field)
            throw POV_EXCEPTION_STRING("Surface light field was not prepared before tracing.");
        sample = EmitterSample{};
        if (scale == 0.0 || (field->IsConstant() && field->ConstantColour().IsZero())) return false;
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        SurfaceGeometrySample surface;
        if (!geometry.SampleToward(from, ray, td, draw.choose, draw.x, draw.y, surface, &guide, true)) return false;
        sample.position = surface.intersection.IPoint;
        sample.normal = surface.normal;
        sample.surfaceObject = surface.intersection.Object;
        sample.pdf = 1.0 / surface.areaWeight;
        sample.weight = field->Emission(surface.intersection, surface.normal, td) * (scale * surface.solidAngleWeight);
        return !sample.weight.IsZero();
    }

    double PhotonDirectionWeight(const EmitterSample& sample, const Vector3d& outgoing) const override
    {
        return std::max(0.0, dot(sample.normal, outgoing));
    }
    Vector3d Direction(const EmitterSample& sample, double u, double v, double& pdf) const override
    {
        const double z = std::sqrt(1.0 - u), r = std::sqrt(u), phi = 2.0 * M_PI * v;
        const Vector3d helper = std::fabs(sample.normal[Z]) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(0, 1, 0);
        const Vector3d tangent = cross(helper, sample.normal).normalized();
        pdf = z / M_PI;
        return sample.normal * z + tangent * (r * std::cos(phi)) + cross(sample.normal, tangent) * (r * std::sin(phi));
    }

private:
    // The mixture with the least summed relative variance at probe receivers around the emitter, scored from draws of an
    // even mixture; the kernels, which cover every direction, keep at least a tenth.
    void ChooseShares(SurfaceAngularGuide& target, const PreparedSurfaceInput& input, TraceThreadData* td,
                      const std::function<void()>& cooperate) const
    {
        const double reach = target.ConeReach();
        if (!(reach > 0.0) || !std::isfinite(reach)) return;
        target.SetShares(1.0 / 3.0, 1.0 / 3.0);
        constexpr unsigned directions = 16, draws = 64;
        const double distances[] = {0.5, 1.25, 3.0, 12.0};
        struct Score { double value, pdfs[3]; };
        struct Probe { std::vector<Score> scores; bool leaves = false; double mean = 0.0; };
        std::vector<Probe> probes;
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        for (unsigned d = 0; d < directions; ++d)
            for (double distance : distances)
            {
                if (cooperate) cooperate();
                const double z = 1.0 - (2.0 * d + 1.0) / directions, r = std::sqrt(1.0 - z * z), phi = d * 2.399963229728653;
                const Vector3d from = target.ConeCentre() + Vector3d(r * std::cos(phi), r * std::sin(phi), z) * (distance * reach);
                Probe probe;
                for (unsigned j = 0; j < draws; ++j)
                {
                    const auto key = DeriveKey(UINT64_C(0x636f6e6573686172), kDrawEmitter, probes.size() * draws + j);
                    SurfaceGeometrySample surface;
                    Score score{0.0, {0.0, 0.0, 0.0}};
                    if (geometry.SampleToward(from, ray, td, Draw(key, kDrawEmitter, 0), Draw(key, kDrawEmitter, 1),
                                              Draw(key, kDrawEmitter, 2), surface, &target, true))
                    {
                        score.value = input.Emission(surface.intersection, surface.normal, td).Greyscale() * surface.solidAngleWeight;
                        probe.leaves = target.StrategyPdfs(from, ray.Direction, score.pdfs, td->surfaceAngularGuideCache);
                    }
                    probe.mean += score.value / draws;
                    if (score.value > 0.0) probe.scores.push_back(score);
                }
                if (probe.mean > 0.0 && std::isfinite(probe.mean)) probes.push_back(std::move(probe));
            }
        double best = HUGE_VAL, bestCone = 0.0, bestLeaf = 0.0;
        for (int c = 0; c <= 18; ++c)
            for (int l = 0; c + l <= 18; ++l)
            {
                double total = 0.0;
                for (const Probe& probe : probes)
                {
                    const double cone = c * 0.05, leaf = probe.leaves ? l * 0.05 : 0.0, even = probe.leaves ? 1.0 / 3.0 : 0.0;
                    double moment = 0.0;
                    for (const Score& score : probe.scores)
                    {
                        const double drawn = score.pdfs[0] / 3.0 + even * score.pdfs[1] + (1.0 - 1.0 / 3.0 - even) * score.pdfs[2];
                        const double mixed = cone * score.pdfs[0] + leaf * score.pdfs[1] + (1.0 - cone - leaf) * score.pdfs[2];
                        moment += score.value * score.value * drawn / mixed;
                    }
                    total += moment / draws / (probe.mean * probe.mean);
                }
                if (total < best) { best = total; bestCone = c * 0.05; bestLeaf = l * 0.05; }
            }
        target.SetShares(bestCone, bestLeaf);
    }

    ObjectPtr owner;
    std::shared_ptr<const MediaLight> settings;
    SurfaceGeometrySampler geometry;
    Vector3d centre;
    double radius;
    mutable std::shared_ptr<const PreparedSurfaceInput> field;
    mutable MathColour intensity;
    mutable double scale = 1.0;
    mutable SurfaceAngularGuide guide;
    mutable std::mutex prepareMutex;
};

}
#endif
