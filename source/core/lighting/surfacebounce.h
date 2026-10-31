#ifndef POVRAY_CORE_SURFACEBOUNCE_H
#define POVRAY_CORE_SURFACEBOUNCE_H

#include "core/configcore.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/surfaceemitter.h"
#include <map>
#include <set>
#include <tuple>

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
    virtual MathColour Sample(const Intersection&, const Vector3d&, const Vector3d&) const = 0;
    virtual MathColour IntegratedRadiance() const = 0;
    virtual void AccumulateGuide(SurfaceAngularGuide&, double scale = 1.0) const {}
};

class PreparedSurfaceBounceField final : public SurfaceBounceRadiance
{
public:
    using CollapsedOwners = std::set<ObjectPtr>;
    struct AngularTerm final { MathColour amplitude; double power; };
    PreparedSurfaceBounceField(const std::vector<SurfaceBounceNode>& samples, std::vector<MathColour> radiance, std::size_t trials,
        const CollapsedOwners& collapsedOwners = {}) :
        samples(samples), radiance(std::move(radiance)), collapsedOwners(collapsedOwners), trials(trials)
    {
        if (samples.size() != this->radiance.size() || samples.empty() || trials == 0)
            throw std::invalid_argument("Surface bounce field requires one radiance per surface node");
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            charts[Chart(samples[i].intersection)].push_back(i);
            integral += this->radiance[i] * samples[i].areaWeight / double(trials);
        }
    }

    PreparedSurfaceBounceField(const std::vector<SurfaceBounceNode>& samples,
        std::vector<std::vector<AngularTerm>> terms, std::size_t trials, const CollapsedOwners& collapsedOwners = {}) :
        PreparedSurfaceBounceField(samples, std::vector<MathColour>(samples.size()), trials, collapsedOwners)
    {
        if (terms.size() != samples.size()) throw std::invalid_argument("Surface angular node count differs");
        angular = std::move(terms);
        for (std::size_t i = 0; i < samples.size(); ++i)
            for (const auto& term : angular[i])
                integral += term.amplitude * (2.0 / (term.power + 2.0)) * samples[i].areaWeight / double(trials);
    }

    MathColour IntegratedRadiance() const override { return integral; }
    void AccumulateGuide(SurfaceAngularGuide& guide, double scale = 1.0) const override
    {
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            MathColour power = radiance[i];
            if (!angular.empty())
                for (const auto& term : angular[i]) power += term.amplitude * (2.0 / (term.power + 2.0));
            guide.Add(samples[i].intersection.IPoint, power.Greyscale() * samples[i].areaWeight * scale / double(trials), samples[i].normal);
        }
    }
    MathColour Sample(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing) const override
    {
        if (!(dot(normal, outgoing) > 0.0)) return MathColour();
        const auto chart = charts.find(Chart(hit));
        if (chart == charts.end())
            throw std::runtime_error("Surface bounce preparation did not cover this primitive chart");
        double nearest = HUGE_VAL;
        for (std::size_t i : chart->second)
        {
            const double distance2 = (samples[i].intersection.IPoint - hit.IPoint).lengthSqr();
            if (distance2 == 0.0) return NodeRadiance(i, dot(normal, outgoing));
            nearest = std::min(nearest, distance2);
        }
        MathColour result;
        double total = 0.0;
        for (std::size_t i : chart->second)
        {
            const double ratio = nearest / (samples[i].intersection.IPoint - hit.IPoint).lengthSqr();
            const double alignment = std::max(0.0, dot(normal, samples[i].normal));
            const double weight = ratio * ratio * (1.0e-6 + alignment * alignment);
            result += NodeRadiance(i, dot(normal, outgoing)) * weight;
            total += weight;
        }
        if (!(total > 0.0)) throw std::runtime_error("Surface bounce interpolation has no finite support");
        return result / total;
    }

private:
    using ChartKey = std::tuple<ObjectPtr, const void*>;
    ChartKey Chart(const Intersection& hit) const
    {
        return ChartKey(hit.Object, collapsedOwners.count(hit.Object) ? nullptr : hit.Pointer);
    }
    std::vector<SurfaceBounceNode> samples;
    std::vector<MathColour> radiance;
    std::vector<std::vector<AngularTerm>> angular;
    CollapsedOwners collapsedOwners;
    MathColour NodeRadiance(std::size_t i, double cosine) const
    {
        if (angular.empty()) return radiance[i];
        MathColour result;
        for (const auto& term : angular[i]) result += term.amplitude * std::pow(cosine, term.power);
        return result;
    }
    MathColour integral;
    std::size_t trials;
    std::map<ChartKey, std::vector<std::size_t>> charts;
};

class PreparedSurfaceBounceResponse final
{
public:
    static std::shared_ptr<const PreparedSurfaceBounceResponse> Prepare(ObjectPtr owner, TraceThreadData* td,
        std::size_t count = 4096, const std::function<void()>& cooperate = {})
    {
        if (!td || count == 0) throw std::invalid_argument("Surface bounce requires preparation samples");
        std::shared_ptr<PreparedSurfaceBounceResponse> result(new PreparedSurfaceBounceResponse());
        result->trials = count;
        SurfaceGeometrySampler geometry(owner);
        if (!geometry.IsValid()) throw std::invalid_argument("Surface bounce requires finite surface geometry");
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (cooperate && i % 256 == 0) cooperate();
            const auto key = DeriveKey(UINT64_C(0x73757266626f756e), kDrawEmitter, i);
            SurfaceGeometrySample sample;
            if (!geometry.SampleArea(ray, td, (i + 0.5) / count, Draw(key, kDrawEmitter, 0),
                                     Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), sample)) continue;
            result->nodes.push_back(SurfaceBounceNode{sample.intersection, sample.normal, sample.areaWeight});
            result->response.push_back(PreparedSurfaceInput::DiffuseLayersAt(sample.intersection, sample.normal, td));
        }
        std::set<std::tuple<ObjectPtr, const void*>> covered;
        std::set<ObjectPtr> sampledObjects;
        for (const auto& node : result->nodes)
        {
            covered.emplace(node.intersection.Object, node.intersection.Pointer);
            sampledObjects.insert(node.intersection.Object);
        }
        std::function<void(ObjectPtr)> cover = [&](ObjectPtr object)
        {
            if (CSG* csg = dynamic_cast<CSG*>(object))
            {
                for (ObjectPtr child : csg->children) cover(child);
                return;
            }
            Mesh* mesh = dynamic_cast<Mesh*>(object);
            const bool uniformMesh = mesh && object == owner && PreparedSurfaceInput::UniformMeshMaterial(mesh);
            if (uniformMesh) result->collapsedOwners.insert(object);
            if (!mesh || uniformMesh)
            {
                if ((!uniformMesh && object == owner) || (uniformMesh ? sampledObjects.count(object) :
                    covered.count(std::make_tuple(object, static_cast<const void*>(nullptr))))) return;
                SurfaceGeometrySampler leaf(object);
                if (!leaf.IsValid()) return;
                for (unsigned attempt = 0; attempt < 256; ++attempt)
                {
                    const auto key = DeriveKey(UINT64_C(0x6c656166636f7665), kDrawEmitter, attempt);
                    SurfaceGeometrySample sample;
                    if (!leaf.SampleArea(ray, td, (attempt + 0.5) / 256, Draw(key, kDrawEmitter, 0),
                                         Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), sample)) continue;
                    if (object != owner)
                    {
                        const double epsilon = std::max(1.0e-9, geometry.Radius() * 1.0e-7);
                        const bool plus = Inside_Object(sample.intersection.IPoint + sample.normal * epsilon, owner, td);
                        const bool minus = Inside_Object(sample.intersection.IPoint - sample.normal * epsilon, owner, td);
                        if (plus == minus) continue;
                        if (plus) sample.normal.invert();
                        sample.intersection.Csg = owner;
                    }
                    result->nodes.push_back(SurfaceBounceNode{sample.intersection, sample.normal, 0.0});
                    result->response.push_back(PreparedSurfaceInput::DiffuseLayersAt(sample.intersection, sample.normal, td));
                    break;
                }
                return;
            }
            mesh->Resolve();
            for (MeshIndex face = 0; face < mesh->Data->Number_Of_Triangles; ++face)
            {
                const MESH_TRIANGLE* triangle = &mesh->Data->Triangles[face];
                if (covered.count(std::make_tuple(object, static_cast<const void*>(triangle)))) continue;
                const Vector3d a(mesh->Data->Vertices[triangle->P1()]);
                const Vector3d b(mesh->Data->Vertices[triangle->P2()]);
                const Vector3d c(mesh->Data->Vertices[triangle->P3()]);
                for (unsigned attempt = 0; attempt < 16; ++attempt)
                {
                    const auto key = DeriveKey(UINT64_C(0x6368617274636f76), kDrawEmitter, face * 16 + attempt);
                    const double root = std::sqrt(Draw(key, kDrawEmitter, 0));
                    const double v = Draw(key, kDrawEmitter, 1);
                    Vector3d point = a * (1.0 - root) + b * (root * (1.0 - v)) + c * (root * v);
                    if (mesh->Trans) MTransPoint(point, point, mesh->Trans);
                    Intersection hit(0.0, point, object);
                    hit.Pointer = triangle;
                    if (object != owner) hit.Csg = owner;
                    Vector3d normal;
                    mesh->Normal(normal, &hit, td);
                    if (Test_Flag(object, INVERTED_FLAG)) normal.invert();
                    normal.normalize();
                    if (object != owner)
                    {
                        const double epsilon = std::max(1.0e-9, geometry.Radius() * 1.0e-7);
                        const bool plus = Inside_Object(point + normal * epsilon, owner, td);
                        const bool minus = Inside_Object(point - normal * epsilon, owner, td);
                        if (plus == minus) continue;
                        if (plus) normal.invert();
                    }
                    result->nodes.push_back(SurfaceBounceNode{hit, normal, 0.0});
                    result->response.push_back(PreparedSurfaceInput::DiffuseLayersAt(hit, normal, td));
                    break;
                }
            }
        };
        cover(owner);
        if (result->nodes.empty()) throw std::runtime_error("Surface bounce preparation found no surface samples");
        return result;
    }

    const std::vector<SurfaceBounceNode>& Nodes() const { return nodes; }
    const PreparedSurfaceBounceField::CollapsedOwners& CollapsedChartOwners() const { return collapsedOwners; }
    std::shared_ptr<const PreparedSurfaceBounceField> Scatter(const std::vector<std::vector<BounceIncidentBeam>>& beams) const
    {
        if (beams.size() != nodes.size()) throw std::invalid_argument("Surface bounce incident node count differs");
        std::vector<std::vector<PreparedSurfaceBounceField::AngularTerm>> terms(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i)
            for (const auto& layer : response[i])
            {
                MathColour incoming;
                for (const auto& beam : beams[i])
                {
                    const double cosine = -dot(nodes[i].normal, beam.direction);
                    const double diffuse = (cosine < 0.0 ? layer.back : layer.front) * layer.adjust;
                    if (diffuse > 0.0)
                        incoming += beam.angularWeight * (std::pow(std::fabs(cosine), layer.brilliance) * diffuse);
                }
                const double adjustment = layer.outgoing == 1.0 ? 1.0 : (layer.outgoing + 7.0) / 8.0;
                terms[i].push_back({incoming * layer.colour * adjustment, layer.outgoing - 1.0});
            }
        return std::make_shared<const PreparedSurfaceBounceField>(nodes, std::move(terms), trials, collapsedOwners);
    }

private:
    PreparedSurfaceBounceField::CollapsedOwners collapsedOwners;
    std::vector<SurfaceBounceNode> nodes;
    std::vector<std::vector<PreparedSurfaceInput::DiffuseLayer>> response;
    std::size_t trials;
};

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
    MathColour Sample(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing) const override
    {
        MathColour result;
        for (const auto& field : fields) if (field) result += field->Sample(hit, normal, outgoing);
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
        this->field->AccumulateGuide(guide);
        if (includePrimary) this->primary->AccumulateSurfaceGuide(guide);
        guide.Prepare();
        intensity = this->field->IntegratedRadiance() + (includePrimary ? this->primary->Intensity() : MathColour());
    }
    bool IsSurface() const override { return true; }
    void AccumulateSurfaceGuide(SurfaceAngularGuide& target) const override { target.AddScaled(guide, 1.0); }
    const Vector3d& Centre() const override { return centre; }
    double Radius() const override { return radius; }
    const MathColour& Intensity() const override { return intensity; }
    MathColour SurfaceRadianceAt(const Intersection& hit, const Vector3d& normal, const Vector3d& outgoing) const override
    {
        return field->Sample(hit, normal, outgoing) +
            (includePrimary ? primary->SurfaceRadianceAt(hit, normal, outgoing) : MathColour());
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
        sample.weight = SurfaceRadianceAt(surface.intersection, surface.normal, surface.normal) * surface.areaWeight;
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
        sample.weight = SurfaceRadianceAt(surface.intersection, surface.normal, -ray.Direction) * surface.solidAngleWeight;
        return !sample.weight.IsZero();
    }
    MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing) const override
    {
        if (!(sample.pdf > 0.0)) return MathColour();
        if (!sample.surfaceIntersection) throw std::runtime_error("Surface photon sample is missing its geometry chart");
        return SurfaceRadianceAt(*sample.surfaceIntersection, sample.normal, outgoing) * (std::max(0.0, dot(sample.normal, outgoing)) / sample.pdf);
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
