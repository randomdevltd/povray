#ifndef POVRAY_CORE_SURFACEBOUNCE_H
#define POVRAY_CORE_SURFACEBOUNCE_H

#include "core/configcore.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/surfaceemitter.h"
#include "core/lighting/surfacenodes.h"
#include "core/shape/blob.h"
#include <set>

namespace pov
{

struct SurfaceBounceNode final
{
    Intersection intersection;
    Vector3d normal;
    double areaWeight;
};

class SurfaceBounceRadiance
{
public:
    virtual ~SurfaceBounceRadiance() = default;
    virtual MathColour Sample(const Intersection&, const Vector3d&, const Vector3d&, TraceThreadData*) const = 0;
    virtual MathColour IntegratedRadiance() const = 0;
    virtual void AccumulateGuide(SurfaceAngularGuide&, double scale = 1.0) const {}
};

/// The interpolated colour, kept within its neighbours' range per channel where the fitted plane overshoots.
template<class Value>
MathColour Blend(const SurfaceNodeIndex::Weights& weights, const Value& value)
{
    MathColour result, low, high;
    bool first = true;
    for (unsigned i = 0; i < weights.count; ++i)
    {
        const MathColour& v = value(weights.node[i]);
        result += v * weights.weight[i];
        if (!(weights.kernel[i] > 0.0)) continue;
        for (int c = 0; c < MathColour::channels; ++c)
        {
            low[c] = first ? v[c] : std::min(low[c], v[c]);
            high[c] = first ? v[c] : std::max(high[c], v[c]);
        }
        first = false;
    }
    for (int c = 0; c < MathColour::channels; ++c)
        result[c] = first ? 0.0 : std::max(low[c], std::min(high[c], result[c]));
    return result;
}

/// Interpolates a value per node of a surface; the field's power is each node's value integral over its area.
class SurfaceNodeGuide
{
public:
    SurfaceNodeGuide(std::shared_ptr<const SurfaceNodeIndex> index, std::vector<float> power, const MathColour& integral) :
        index(std::move(index)), power(std::move(power)), integral(integral)
    {
        if (!this->index || this->power.size() != this->index->Size())
            throw std::invalid_argument("Surface bounce field requires one power per node");
    }
    void Guide(SurfaceAngularGuide& guide, double scale) const
    {
        for (std::size_t i = 0; i < power.size(); ++i)
            guide.Add(index->Point(i), power[i] * scale, index->Normal(i));
    }
protected:
    std::shared_ptr<const SurfaceNodeIndex> index;
    std::vector<float> power;
    MathColour integral;
};

/// Light arriving at the nodes, as front and back sums of cos^brilliance per brilliance class; the surface's own diffuse
/// response is evaluated exactly where the field is read.
class SurfaceIrradianceField final : public SurfaceBounceRadiance, private SurfaceNodeGuide
{
public:
    SurfaceIrradianceField(std::shared_ptr<const SurfaceNodeIndex> index, std::vector<double> classes,
        std::vector<MathColour> irradiance, std::vector<float> power, const MathColour& integral) :
        SurfaceNodeGuide(std::move(index), std::move(power), integral), classes(std::move(classes)), irradiance(std::move(irradiance))
    {
        if (this->classes.empty() || this->irradiance.size() != this->index->Size() * 2 * this->classes.size())
            throw std::invalid_argument("Surface irradiance requires front and back sums per class and node");
    }
    MathColour IntegratedRadiance() const override { return integral; }
    void AccumulateGuide(SurfaceAngularGuide& guide, double scale = 1.0) const override { Guide(guide, scale); }
    MathColour Sample(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing, TraceThreadData* td) const override
    {
        const double cosine = dot(normal, outgoing);
        if (!(cosine != 0.0)) return MathColour();
        SurfaceNodeIndex::Weights weights;
        index->Interpolate(hit.IPoint, normal, weights);
        const std::size_t stride = 2 * classes.size();
        return Radiance(hit, normal, cosine, [&](std::size_t j)
        {
            return Blend(weights, [&](std::uint32_t node) -> const MathColour& { return irradiance[node * stride + j]; });
        }, td);
    }
    /// The radiance leaving a point toward the side of `cosine`, lit by `sum(j)`, the front then back sums of class j / 2;
    /// as in POV's shading, the side it leaves is the front, so either side of a sheet reflects.
    template<class Sum>
    MathColour Radiance(const Intersection& hit, const Vector3d& normal, double cosine, const Sum& sum, TraceThreadData* td) const
    {
        const std::size_t side = cosine < 0.0 ? 1 : 0;
        MathColour result;
        for (const auto& layer : PreparedSurfaceInput::DiffuseLayersAt(hit, normal, td, false))
        {
            const std::size_t c = Class(classes, layer.brilliance);
            const MathColour incoming = sum(2 * c + side) * std::max(0.0, layer.front * layer.adjust) +
                                        sum(2 * c + 1 - side) * std::max(0.0, layer.back * layer.adjust);
            const double adjustment = layer.outgoing == 1.0 ? 1.0 : (layer.outgoing + 7.0) / 8.0;
            result += incoming * layer.colour *
                      (adjustment * (layer.outgoing == 1.0 ? 1.0 : std::pow(std::fabs(cosine), layer.outgoing - 1.0)));
        }
        return result;
    }
    /// The light leaving both sides of a point per unit area, lit by `sums` laid out as the nodes' are.
    static MathColour Exitance(const Intersection& hit, const Vector3d& normal, const std::vector<double>& classes,
                               const MathColour* sums, TraceThreadData* td)
    {
        MathColour result;
        for (const auto& layer : PreparedSurfaceInput::DiffuseLayersAt(hit, normal, td))
        {
            const std::size_t c = Class(classes, layer.brilliance);
            const MathColour incoming = (sums[2 * c] + sums[2 * c + 1]) *
                                        (std::max(0.0, layer.front * layer.adjust) + std::max(0.0, layer.back * layer.adjust));
            const double adjustment = layer.outgoing == 1.0 ? 1.0 : (layer.outgoing + 7.0) / 8.0;
            result += incoming * layer.colour * (adjustment * 2.0 / (layer.outgoing + 1.0));
        }
        return result;
    }
private:
    static std::size_t Class(const std::vector<double>& classes, double brilliance)
    {
        for (std::size_t c = 0; c < classes.size(); ++c)
            if (classes[c] == brilliance) return c;
        throw std::logic_error("Surface bounce lookup found a finish its preparation did not");
    }
    std::vector<double> classes;
    std::vector<MathColour> irradiance;
};

/// Radiance per node, read as the nodes' interpolation.
class SurfaceRadianceNodeField final : public SurfaceBounceRadiance, private SurfaceNodeGuide
{
public:
    SurfaceRadianceNodeField(std::shared_ptr<const SurfaceNodeIndex> index, std::vector<MathColour> radiance,
        std::vector<float> power, const MathColour& integral) :
        SurfaceNodeGuide(std::move(index), std::move(power), integral), radiance(std::move(radiance))
    {
        if (this->radiance.size() != this->index->Size())
            throw std::invalid_argument("Surface bounce field requires one radiance per node");
    }
    MathColour IntegratedRadiance() const override { return integral; }
    void AccumulateGuide(SurfaceAngularGuide& guide, double scale = 1.0) const override { Guide(guide, scale); }
    MathColour Sample(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing, TraceThreadData*) const override
    {
        if (!(dot(normal, outgoing) > 0.0)) return MathColour();
        SurfaceNodeIndex::Weights weights;
        index->Interpolate(hit.IPoint, normal, weights);
        return Blend(weights, [&](std::uint32_t node) -> const MathColour& { return radiance[node]; });
    }
private:
    std::vector<MathColour> radiance;
};

/// The brilliance of every diffuse finish an object's materials can show, sorted.
inline std::vector<double> SurfaceDiffuseClasses(ConstObjectPtr object)
{
    std::set<const TEXTURE*> visited;
    std::set<double> classes;
    std::function<void(const TEXTURE*)> texture = [&](const TEXTURE* t)
    {
        if (!t || !visited.insert(t).second) return;
        if (t->Finish) classes.insert(t->Finish->Brilliance);
        if (t->Blend_Map)
            for (const auto& entry : t->Blend_Map->Blend_Map_Entries) texture(entry.Vals);
        for (const TEXTURE* material : t->Materials) texture(material);
        texture(t->Next);
    };
    std::function<void(ConstObjectPtr)> walk = [&](ConstObjectPtr o)
    {
        texture(o->Texture);
        texture(o->Interior_Texture);
        if (const Mesh* mesh = dynamic_cast<const Mesh*>(o))
            for (MeshIndex i = 0; i < mesh->Number_Of_Textures; ++i) texture(mesh->Textures[i]);
        if (const Blob* blob = dynamic_cast<const Blob*>(o))
            for (const TEXTURE* t : blob->Element_Texture) texture(t);
        if (const CompoundObject* compound = dynamic_cast<const CompoundObject*>(o))
            for (ConstObjectPtr child : compound->children) walk(child);
    };
    walk(object);
    if (classes.empty()) classes.insert(1.0);
    return std::vector<double>(classes.begin(), classes.end());
}

class SumSurfaceBounceRadiance final : public SurfaceBounceRadiance
{
public:
    explicit SumSurfaceBounceRadiance(std::vector<std::shared_ptr<const SurfaceBounceRadiance>> fields) : fields(std::move(fields)) {}
    MathColour IntegratedRadiance() const override
    {
        MathColour result;
        for (const auto& field : fields) if (field) result += field->IntegratedRadiance();
        return result;
    }
    void AccumulateGuide(SurfaceAngularGuide& guide, double scale = 1.0) const override
    {
        for (const auto& field : fields) if (field) field->AccumulateGuide(guide, scale);
    }
    MathColour Sample(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing, TraceThreadData* td) const override
    {
        MathColour result;
        for (const auto& field : fields) if (field) result += field->Sample(hit, normal, outgoing, td);
        return result;
    }
private:
    std::vector<std::shared_ptr<const SurfaceBounceRadiance>> fields;
};

class BouncedSurfaceEmitter final : public Emitter
{
public:
    BouncedSurfaceEmitter(std::shared_ptr<const Emitter> primary, ObjectPtr owner,
        std::shared_ptr<const SurfaceBounceRadiance> field, bool includePrimary) :
        primary(std::move(primary)), geometry(owner), field(std::move(field)), includePrimary(includePrimary),
        centre(geometry.Centre()), radius(geometry.Radius()),
        guide(Vector3d(owner->BBox.lowerLeft), Vector3d(owner->BBox.size))
    {
        if (!this->primary || !this->primary->IsSurface() || !this->field || !geometry.IsValid())
            throw std::invalid_argument("Surface bounce emitter requires a prepared surface source and field");
        geometry.ResolveDeferred();
        this->field->AccumulateGuide(guide);
        if (includePrimary) this->primary->AccumulateSurfaceGuide(guide);
        guide.Prepare();
        intensity = this->field->IntegratedRadiance() + (includePrimary ? this->primary->Intensity() : MathColour());
    }
    bool IsSurface() const override { return true; }
    void ResolveGeometry() const override { geometry.ResolveDeferred(); }
    void AccumulateSurfaceGuide(SurfaceAngularGuide& target) const override { target.AddScaled(guide, 1.0); }
    const Vector3d& Centre() const override { return centre; }
    double Radius() const override { return radius; }
    const MathColour& Intensity() const override { return intensity; }
    MathColour RadianceAt(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing, TraceThreadData* td) const
    {
        return field->Sample(hit, normal, outgoing, td) +
            (includePrimary ? primary->SurfaceRadianceAt(hit, normal, outgoing, td) : MathColour());
    }
    EmitterSample Sample(double s, std::uint64_t key, TraceThreadData* td = nullptr) const override
    {
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        SurfaceGeometrySample surface;
        EmitterSample sample{};
        if (!geometry.SampleArea(ray, td, s, Draw(key, kDrawEmitter, 0), Draw(key, kDrawEmitter, 1),
                                 Draw(key, kDrawEmitter, 2), surface)) return sample;
        sample.position = surface.intersection.IPoint;
        sample.normal = surface.normal;
        sample.surfaceObject = surface.intersection.Object;
        sample.surfaceIntersection = std::make_shared<const Intersection>(surface.intersection);
        sample.pdf = 1.0 / surface.areaWeight;
        sample.weight = RadianceAt(surface.intersection, surface.normal, surface.normal, td) * surface.areaWeight;
        return sample;
    }
    bool SampleToward(const Vector3d& from, const EmitterDraw& draw, TraceThreadData* td, EmitterSample& sample) const override
    {
        sample = EmitterSample{};
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        SurfaceGeometrySample surface;
        if (!geometry.SampleToward(from, ray, td, draw.choose, draw.x, draw.y, surface, &guide)) return false;
        sample.position = surface.intersection.IPoint;
        sample.normal = surface.normal;
        sample.surfaceObject = surface.intersection.Object;
        sample.surfaceIntersection = std::make_shared<const Intersection>(surface.intersection);
        sample.pdf = 1.0 / surface.areaWeight;
        sample.weight = RadianceAt(surface.intersection, surface.normal, -ray.Direction, td) * surface.solidAngleWeight;
        return !sample.weight.IsZero();
    }
    MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing) const override
    {
        throw std::logic_error("Surface bounce photon emission needs its thread's data");
    }
    MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing, TraceThreadData* td) const override
    {
        if (!(sample.pdf > 0.0)) return MathColour();
        if (!sample.surfaceIntersection) throw std::runtime_error("Surface photon sample is missing its surface point");
        return RadianceAt(*sample.surfaceIntersection, sample.normal, outgoing, td) * (std::fabs(dot(sample.normal, outgoing)) / sample.pdf);
    }
    Vector3d Direction(const EmitterSample& sample, double u, double v, double& pdf) const override
    {
        return primary->Direction(sample, u, v, pdf);
    }
private:
    std::shared_ptr<const Emitter> primary;
    SurfaceGeometrySampler geometry;
    std::shared_ptr<const SurfaceBounceRadiance> field;
    bool includePrimary;
    Vector3d centre;
    double radius;
    MathColour intensity;
    SurfaceAngularGuide guide;
};

inline std::shared_ptr<const Emitter> MakeBouncedSurfaceEmitter(std::shared_ptr<const Emitter> primary,
    ObjectPtr owner, std::shared_ptr<const SurfaceBounceRadiance> field, bool includePrimary = true)
{
    return std::make_shared<const BouncedSurfaceEmitter>(std::move(primary), owner, std::move(field), includePrimary);
}

}
#endif
