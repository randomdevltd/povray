#ifndef POVRAY_CORE_SURFACEEMITTER_H
#define POVRAY_CORE_SURFACEEMITTER_H

#include "core/configcore.h"
#include "core/lighting/emitter.h"
#include "core/lighting/surfacegeometry.h"
#include "core/material/preparedsurface.h"
#include "core/math/randomsequence.h"
#include "core/render/trace.h"
#include <mutex>

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
    MathColour SurfaceRadianceAt(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing) const override
    {
        if (!field)
            throw POV_EXCEPTION_STRING("Surface light field was not prepared before tracing.");
        return dot(normal, outgoing) > 0.0 ? field->Sample(hit, normal) * scale : MathColour();
    }
    const MathColour& Intensity() const override { return intensity; }
    const Vector3d& Centre() const override { return centre; }
    double Radius() const override { return radius; }

    void ResolveGeometry() const override { geometry.ResolveDeferred(); }

    void PrepareAxis(std::shared_ptr<const PreparedSourceDomain>, TraceThreadData* td,
                     const std::function<void()>& cooperate) const override
    {
        std::lock_guard<std::mutex> lock(prepareMutex);
        if (field) return;
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        const std::shared_ptr<const PreparedSurfaceInput> prepared =
            PreparedSurfaceInput::Prepare(owner, geometry, settings->pigment, settings->useEmission, ray, td);
        SurfaceAngularGuide preparedGuide(Vector3d(owner->BBox.lowerLeft), Vector3d(owner->BBox.size));
        MathColour integral;
        constexpr unsigned count = 16384;
        const double exactArea = geometry.ExactArea();
        const bool zero = prepared->IsConstant() && prepared->ConstantColour().IsZero();
        const bool exact = prepared->IsConstant() && exactArea > 0.0;
        for (unsigned i = 0; !exact && !zero && i < count; ++i)
        {
            if (cooperate && (i % 256 == 0)) cooperate();
            SurfaceGeometrySample sample;
            const auto key = DeriveKey(UINT64_C(0x736f757263656172), kDrawEmitter, i);
            if (geometry.SampleArea(ray, td, (i + 0.5) / count, Draw(key, kDrawEmitter, 0),
                                    Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), sample))
            {
                const MathColour power = prepared->Sample(sample.intersection, sample.normal) * sample.areaWeight;
                integral += power;
                preparedGuide.Add(sample.intersection.IPoint, power.Greyscale() / double(count), sample.normal);
            }
        }
        integral = exact ? prepared->ConstantColour() * exactArea : integral / double(count);
        double preparedScale = settings->brightness;
        if (settings->lumens >= 0.0)
        {
            const double power = M_PI * integral.Greyscale();
            if (settings->lumens > 0.0 && !(power > 0.0))
                throw POV_EXCEPTION_STRING("A surface light stated in lumens requires positive surface emission.");
            preparedScale = settings->lumens == 0.0 ? 0.0 : settings->lumens / power;
        }
        preparedGuide.Prepare();
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
        sample.weight = field->Sample(surface.intersection, surface.normal) * (scale * surface.areaWeight);
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
        if (!geometry.SampleToward(from, ray, td, draw.choose, draw.x, draw.y, surface, &guide)) return false;
        if (!(dot(surface.normal, -ray.Direction) > 0.0)) return false;
        sample.position = surface.intersection.IPoint;
        sample.normal = surface.normal;
        sample.surfaceObject = surface.intersection.Object;
        sample.pdf = 1.0 / surface.areaWeight;
        sample.weight = field->Sample(surface.intersection, surface.normal) * (scale * surface.solidAngleWeight);
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
