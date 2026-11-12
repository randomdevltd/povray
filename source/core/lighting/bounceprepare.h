#ifndef POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H
#define POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H

#include "core/configcore.h"
#include "core/lighting/bouncegrid.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/photons.h"
#include "core/lighting/sharedincidentfield.h"
#include "core/lighting/subsurface.h"
#include "core/lighting/surfacebounce.h"
#include "core/lighting/surfacerefine.h"
#include "core/material/interiorfield.h"
#include "core/material/media.h"
#include "core/material/pigment.h"
#include "core/material/preparedsourcefield.h"
#include "core/material/texture.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/parallel.h"
#include "core/support/preparationprogress.h"
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace pov
{
namespace bounce_prepare
{
struct Cooperate final : Trace::CooperateFunctor
{
    explicit Cooperate(const std::function<void()>& callback) : callback(callback) {}
    void operator()() override
    {
        if (callback)
            callback();
    }
    std::function<void()> callback;
};

struct Context final
{
    Context(std::shared_ptr<SceneData> scene, TraceThreadData* td, const std::function<void()>& callback)
        : cooperate([this, callback]() { if (callback) callback(); CountWork(); }), trace(scene, td, QualityFlags(9), cooperate, media, radiosity),
          media(td, &trace, &photons),
          photons(&scene->GetPreparedSet(0).surfacePhotonMap, scene->photonSettings)
    {
    }
    void Watch(ConstObjectPtr object)
    {
        owner = object;
        work = 0;
    }
    // Bound cooperative trace steps, independent of pauses, pre-emption or machine load.
    static constexpr std::size_t kMaximumGatherSteps = 1048576;
    void CountWork()
    {
        if (!owner)
            return;
        if (work == kMaximumGatherSteps)
            throw std::runtime_error("Surface light preparation exceeded " + std::to_string(kMaximumGatherSteps) +
                " trace steps at one gather on " +
                std::string(owner->source ? *owner->source : "an unnamed object") +
                "; report this scene with its domain_cells and subsurface settings.");
        ++work;
    }
    ConstObjectPtr owner = nullptr;
    std::size_t work = 0;
    Cooperate cooperate;
    Trace::RadiosityFunctor radiosity;
    Trace trace;
    MediaFunction media;
    PhotonGatherer photons;
};

// Thrown by a worker that sees the preparation stopped; the caller's own cooperation then reports the stop.
struct Stopped final : std::runtime_error
{
    Stopped() : std::runtime_error("Bounce preparation was stopped") {}
};

// One Context and thread data per worker; worker 0 borrows the caller's thread data, which the caller leaves alone meanwhile.
struct Workers final
{
    Workers(const std::shared_ptr<SceneData>& scene, TraceThreadData* td) : td(td)
    {
        const std::shared_ptr<std::atomic<bool>> cancelled = scene->meshBuildCancelled;
        stop = [cancelled]()
        {
            if (cancelled->load(std::memory_order_relaxed))
                throw Stopped();
        };
        const std::size_t count = std::max(1u, scene->workThreads);
        for (std::size_t worker = 0; worker < count; ++worker)
        {
            if (worker > 0)
            {
                helpers.emplace_back(new TraceThreadData(scene, worker, false));
                helpers.back()->stochasticRandomSeedBase = td->stochasticRandomSeedBase;
            }
            contexts.emplace_back(new Context(scene, Thread(worker), stop));
        }
    }
    ~Workers()
    {
        contexts.clear();
        for (const auto& helper : helpers)
            td->Stats() += helper->Stats();
    }
    std::size_t Count() const { return contexts.size(); }
    TraceThreadData* Thread(std::size_t worker) const { return worker == 0 ? td : helpers[worker - 1].get(); }
    Context& operator[](std::size_t worker) { return *contexts[worker]; }
    // Each index must write only its own results, so that they do not depend on which worker computed them; the
    // count advances the progress by `units` over all indices.
    template<typename Function>
    void For(std::size_t count, const std::function<void()>& cooperate, const Function& function, std::size_t units)
    {
        // Refinement and subsurface gathers discover work as they go. Count every completed item, including
        // resampling, instead of leaving the display unchanged throughout a long level.
        if (count > units)
        {
            if (td->preparationProgress) td->preparationProgress->AddWork(count - units);
            units = count;
        }
        const auto advance = [count, units](std::size_t index) { return (index + 1) * units / count - index * units / count; };
        try
        {
            ParallelFor(count, contexts.size(), [&](std::size_t index, std::size_t worker)
            {
                stop();
                function(index, worker);
                contexts[worker]->owner = nullptr;
                AdvancePreparation(td->preparationProgress, advance(index));
            }, [&]() { if (cooperate) cooperate(); }, true);
        }
        catch (const Stopped&)
        {
            if (cooperate)
                cooperate();
            throw;
        }
    }
    template<typename Function>
    void For(std::size_t count, const std::function<void()>& cooperate, const Function& function)
    {
        For(count, cooperate, function, count);
    }
    void Step(const std::string& text) const { StepPreparation(td->preparationProgress, text); }

    TraceThreadData* td;
    std::function<void()> stop;
    std::vector<std::unique_ptr<TraceThreadData>> helpers;
    std::vector<std::unique_ptr<Context>> contexts;
};

struct Target final
{
    LightSource* light;
    std::shared_ptr<const Emitter> primary;
    std::shared_ptr<const PreparedSourceDomain> domain;
    std::vector<PreparedBouncePhase> phases;
    std::vector<std::shared_ptr<const PreparedColourField>> coefficients;
    std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> orders;
    std::shared_ptr<const PreparedSourceDistribution> proposal;
    unsigned int passes;
    double gain = 1.0;
    bool surface = false;
    MathColour self = MathColour(1.0);
    bool internalPrimary = false;
    std::size_t part = 0;
    std::vector<std::shared_ptr<const SurfaceBounceRadiance>> surfaceOrders;
};

// Progress units of a surface target's refinement in one order, however many nodes it finds.
constexpr std::size_t kSurfaceUnits = 4096;

inline std::string Of(const char* name, std::size_t index, std::size_t count)
{
    return name + std::to_string(index + 1) + "/" + std::to_string(count);
}

inline bool Receives(ConstObjectPtr owner, const LightSource* light)
{
    if (!light->lightGroupLight)
        return !Test_Flag(owner, NO_GLOBAL_LIGHTS_FLAG);
    return std::find(owner->LLights.begin(), owner->LLights.end(), light) != owner->LLights.end();
}

/// Draws per light at a bounced volume's nodes: its light's samples and its scattering media's largest light_samples.
inline unsigned int BounceGatherDraws(ConstObjectPtr owner)
{
    DBL multiplier = 0.0;
    for (const auto& medium : owner->interior->media)
        if (medium.use_scattering)
            multiplier = std::max(multiplier, medium.Light_Samples);
    return VolumeGatherDraws(owner->interior->light->samples, multiplier > 0.0 ? multiplier : 1.0);
}

inline void CollectObjects(ConstObjectPtr object, std::vector<ConstObjectPtr>& objects,
                           std::unordered_set<ConstObjectPtr>& seen)
{
    if (!object || !seen.insert(object).second)
        return;
    objects.push_back(object);
    if (const auto compound = dynamic_cast<const CompoundObject*>(object))
        for (const auto child : compound->children)
            CollectObjects(child, objects, seen);
}

// Whether a texture, its layers or its material map carry a subsurface method 2 finish.
inline bool CarriesMethod2(const TEXTURE* texture, const SceneData& scene)
{
    for (; texture != nullptr; texture = texture->Next)
    {
        const FINISH* finish = texture->Finish;
        if (finish && finish->UseSubsurface &&
            ((finish->SubsurfaceMethod != 0) ? finish->SubsurfaceMethod : scene.subsurfaceMethod) == kSubsurfaceMethodPointCloud)
            return true;
        for (const TEXTURE* material : texture->Materials)
            if (CarriesMethod2(material, scene))
                return true;
    }
    return false;
}

// Whether an object or any part of it is a method 2 host.
inline bool Method2Host(ConstObjectPtr object, const SceneData& scene)
{
    if (CarriesMethod2(object->Texture, scene) || CarriesMethod2(object->Interior_Texture, scene))
        return true;
    if (const auto compound = dynamic_cast<const CompoundObject*>(object))
        for (const auto child : compound->children)
            if (Method2Host(child, scene))
                return true;
    return false;
}

// Whether two world boxes share a volume wider than float rounding, the test the internal source selection makes.
inline bool BoxesMeet(const BoundingBox& a, const BoundingBox& b)
{
    for (int axis = X; axis <= Z; axis++)
    {
        const double low = std::max(double(a.lowerLeft[axis]), double(b.lowerLeft[axis]));
        const double high = std::min(double(a.lowerLeft[axis]) + a.size[axis], double(b.lowerLeft[axis]) + b.size[axis]);
        const double magnitude = std::max(std::max(fabs(low), fabs(high)), std::max(double(a.size[axis]), double(b.size[axis])));
        if (!(high - low > 4.0 * std::numeric_limits<float>::epsilon() * magnitude))
            return false;
    }
    return true;
}

// An emitting surface whose method 2 material holds a volume emitter exports that emitter's escaping light as its own
// primary; any other surface has none to export and pays nothing for it.
inline bool InternalPrimary(const LightSource& light, const SceneData& scene)
{
    const auto owner = light.emitterContainer;
    return scene.useSubsurface && light.emitter && light.emitter->IsSurface() && owner &&
        owner->surfaceLight && owner->surfaceLight->useEmission && Method2Host(owner, scene) &&
        std::any_of(scene.mediaLights.begin(), scene.mediaLights.end(), [&](const LightSource* source)
            { return !source->Photon_Only && source->emitter && source->emitter->HasVolumeEmission() &&
                     source->emitterContainer && BoxesMeet(source->emitterContainer->BBox, owner->BBox); });
}

inline const MediaLight* BounceSettings(const LightSource& light)
{
    const auto owner = light.emitterContainer;
    if (!owner || !light.emitter)
        return nullptr;
    const auto settings = light.emitter->IsSurface() ? owner->surfaceLight.get()
                                                     : (owner->interior ? owner->interior->light.get() : nullptr);
    return (settings && settings->bouncePasses > 0) ? settings : nullptr;
}

inline bool ConstantOwnerCoefficients(ConstObjectPtr owner, const std::vector<ConstObjectPtr>& objects)
{
    for (const auto& medium : owner->interior->media)
    {
        if (medium.mix != kMediaBlendAdd)
            return false;
        for (const auto pigment : medium.Density)
            if (pigment->Type != PLAIN_PATTERN)
                return false;
    }
    for (const auto object : objects)
        if (object->interior)
        {
            for (const auto& medium : object->interior->media)
                if (medium.mix != kMediaBlendAdd)
                    return false;
            if (object->interior->clears)
                for (const auto& medium : owner->interior->media)
                    if (medium.use_scattering &&
                        (medium.priority < 0 || (medium.priority == 0 &&
                                                 object->interior->precedence > owner->interior->precedence)))
                        return false;
        }
    return true;
}

inline void PrepareCoefficients(Target& target, Workers& workers,
                                const std::vector<ConstObjectPtr>& objects,
                                const std::function<void()>& cooperate)
{
    const auto owner = target.light->emitterContainer;
    for (const auto& medium : owner->interior->media)
        if (medium.use_scattering)
        {
            const PreparedBouncePhase phase{medium.Type, medium.Eccentricity};
            if (std::find(target.phases.begin(), target.phases.end(), phase) == target.phases.end())
                target.phases.push_back(phase);
        }
    const bool constant = ConstantOwnerCoefficients(owner, objects);
    std::vector<std::vector<MathColour>> values(target.phases.size());
    for (auto& phase : values)
        phase.resize(constant ? 1 : target.domain->NodeCount());
    const std::size_t count = constant ? 1 : target.domain->NodeCount();
    workers.For(count, cooperate, [&](size_t node, size_t worker)
    {
        TraceThreadData* const td = workers.Thread(worker);
        const auto& point = target.domain->Node(node);
        RayInteriorVector interiors;
        if (!constant)
            workers[worker].trace.FindContainingInteriors(point, interiors);
        // A node just outside the owner extends its media, as its primary field does, so cells across the boundary keep them.
        if (std::find(interiors.begin(), interiors.end(), owner->interior.get()) == interiors.end())
            interiors.push_back(owner->interior.get());
        for (const auto& component : EvaluateOwnerBounceScattering(interiors, *owner->interior, point, td))
        {
            const auto found = std::find(target.phases.begin(), target.phases.end(),
                                         PreparedBouncePhase{component.type, component.eccentricity});
            if (found != target.phases.end())
                values[found - target.phases.begin()][node] += component.coefficient * target.gain;
        }
    }, target.domain->NodeCount());
    for (auto& phase : values)
        if (constant)
            target.coefficients.push_back(std::make_shared<const PreparedConstantColourField>(phase.front()));
        else
            target.coefficients.push_back(
                std::make_shared<const PreparedSourceColourField>(target.domain, std::move(phase)));
}

// Gives every worker one fresh cache of subsurface boundary and layer tables while it lasts.
struct SubsurfaceScope final
{
    SubsurfaceScope(Workers& workers, const std::shared_ptr<SceneData>& scene) : workers(workers)
    {
        const auto cache = std::make_shared<SubsurfaceCache>(scene->subsurfaceMemory, scene->subsurfaceSpacing);
        for (std::size_t worker = 0; worker < workers.Count(); ++worker)
        {
            saved.push_back(workers.Thread(worker)->subsurfaceCache);
            workers.Thread(worker)->subsurfaceCache = cache;
        }
    }
    ~SubsurfaceScope()
    {
        for (std::size_t worker = 0; worker < saved.size(); ++worker)
            workers.Thread(worker)->subsurfaceCache = saved[worker];
    }
    Workers& workers;
    std::vector<std::shared_ptr<SubsurfaceCache>> saved;
};

// The subsurface light leaving one part's surface bounce nodes, diffused between them from the light the refinement found
// entering each; the workers share one cache of boundary and layer tables.
inline std::shared_ptr<const SurfaceBounceRadiance> PrepareSubsurfaceBounce(
    Workers& workers, const std::shared_ptr<SceneData>& scene, const surface_refine::Result& result,
    const Sources& sources, const std::function<void()>& cooperate, std::size_t units, unsigned samples, bool boundary = true)
{
    const SubsurfaceScope scope(workers, scene);
    std::vector<std::unique_ptr<Trace>> traces;
    for (std::size_t worker = 0; worker < workers.Count(); ++worker)
    {
        TraceThreadData* const thread = workers.Thread(worker);
        Context& context = workers[worker];
        traces.emplace_back(new Trace(scene, thread, QualityFlags(9), context.cooperate, context.media, context.radiosity));
    }
    Trace::SurfaceSubsurfaceField field;
    for (const auto& trace : traces)
        trace->BeginSurfaceSubsurface(field);
    const auto& nodes = result.nodes;
    const std::size_t count = nodes.size();
    field.Build(nodes, result.entry, 1);
    std::vector<double> spacing(count), levels;
    for (std::size_t t = 0; t < count; ++t)
        spacing[result.index->Source()[t]] = result.index->Spacing(t);
    levels = spacing;
    std::sort(levels.begin(), levels.end(), std::greater<double>());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    // Light diffuses within one object's material, as the field's trees are built.
    std::vector<std::pair<const void*, const void*>> keys;
    std::vector<std::size_t> group(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const Intersection& hit = nodes[i].intersection;
        const std::pair<const void*, const void*> key(hit.Csg ? (const void*)hit.Csg : (const void*)hit.Object, hit.Object->interior.get());
        group[i] = std::find(keys.begin(), keys.end(), key) - keys.begin();
        if (group[i] == keys.size()) keys.push_back(key);
    }
    // Diffusion blurs the light leaving over its length, so it is found at nodes no finer than a quarter of that length,
    // level by level, and interpolated between them at finer ones.
    std::vector<MathColour> radiance(count);
    std::vector<double> length(count, HUGE_VAL);
    std::vector<std::vector<std::size_t>> found(keys.size());
    std::vector<std::shared_ptr<const SurfaceNodeIndex>> foundIndex(keys.size());
    std::vector<std::size_t> later;
    for (double level : boundary ? levels : std::vector<double>())
    {
        std::vector<std::size_t> now;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (spacing[i] != level) continue;
            const auto& index = foundIndex[group[i]];
            const bool fine = index && level < 0.25 * length[found[group[i]][index->Source()[
                index->Nearest(nodes[i].intersection.IPoint, nodes[i].normal)]]];
            (fine ? later : now).push_back(i);
        }
        workers.For(now.size(), cooperate, [&](size_t k, size_t worker)
        {
            workers[worker].Watch(nodes[now[k]].intersection.Object);
            radiance[now[k]] = traces[worker]->SurfaceSubsurfaceNode(nodes[now[k]], now[k], 0, length[now[k]]);
        }, 0);
        for (std::size_t i : now) found[group[i]].push_back(i);
        for (std::size_t g = 0; g < keys.size(); ++g)
        {
            if (found[g].empty() || (foundIndex[g] && foundIndex[g]->Size() == found[g].size())) continue;
            std::vector<Vector3d> points, normals;
            std::vector<double> spacings;
            for (std::size_t i : found[g])
            {
                points.push_back(nodes[i].intersection.IPoint);
                normals.push_back(nodes[i].normal);
                spacings.push_back(spacing[i]);
            }
            foundIndex[g] = std::make_shared<const SurfaceNodeIndex>(points, normals, spacings);
        }
    }
    workers.For(later.size(), cooperate, [&](size_t k, size_t)
    {
        const std::size_t i = later[k], g = group[i];
        SurfaceNodeIndex::Weights weights;
        foundIndex[g]->Interpolate(nodes[i].intersection.IPoint, nodes[i].normal, weights);
        radiance[i] = Blend(weights, [&](std::uint32_t t) -> const MathColour& { return radiance[found[g][foundIndex[g]->Source()[t]]]; });
    }, 0);
    // Light emitted within the material, each source at its share: a light's own earlier bounce at its self share.
    std::vector<MathColour> shares;
    for (std::size_t s = 0; s < sources.lights.size(); ++s)
    {
        const MathColour share = sources.scales.empty() ? MathColour(1.0) : sources.scales[s];
        const LightSource* light = sources.lights[s];
        if (light->emitter && light->emitter->HasVolumeEmission() && !share.IsZero() &&
            std::none_of(shares.begin(), shares.end(), [&](const MathColour& seen) { return (seen - share).IsZero(); }))
            shares.push_back(share);
    }
    for (const MathColour& share : shares)
    {
        std::vector<LightSource*> lights;
        for (std::size_t s = 0; s < sources.lights.size(); ++s)
            if (((sources.scales.empty() ? MathColour(1.0) : sources.scales[s]) - share).IsZero())
                lights.push_back(sources.lights[s]);
        for (const auto& trace : traces)
            trace->BeginSurfaceSubsurface(field, lights, true);
        workers.For(count, cooperate, [&](size_t i, size_t worker)
        {
            workers[worker].Watch(nodes[i].intersection.Object);
            double unused;
            radiance[i] += share * traces[worker]->SurfaceSubsurfaceNode(nodes[i], i, 0, unused, samples);
        }, 0);
    }
    AdvancePreparation(workers.td->preparationProgress, units);
    traces.clear();
    return Trace::SurfaceSubsurfaceRadiance(result.index, radiance, result.etas, result.areas);
}

// Bounce progress units, as the passes below count them; zero while a volume target's domain is unprepared.
inline std::size_t BounceUnits(const SceneData& scene)
{
    std::size_t units = 0;
    std::vector<std::shared_ptr<const PreparedSourceDomain>> domains;
    for (const LightSource* light : scene.mediaLights)
    {
        const MediaLight* const settings = BounceSettings(*light);
        const bool primary = InternalPrimary(*light, scene);
        if (!settings && !primary)
            continue;
        if (light->emitter->IsSurface())
        {
            units += 1 + (primary ? 2 * kSurfaceUnits : 0) +
                (settings ? settings->bouncePasses : 0) * (kSurfaceUnits * (scene.useSubsurface ? 2 : 1) + 1);
            continue;
        }
        const auto axis = light->emitter->PreparedAxis();
        const auto domain = axis ? axis->Domain() : nullptr;
        if (!domain)
            return 0;
        units += domain->NodeCount() + settings->bouncePasses * (domain->NodeCount() + domain->CellCount());
        if (std::find(domains.begin(), domains.end(), domain) == domains.end())
        {
            domains.push_back(domain);
            units += domain->CellCount();
        }
    }
    return units;
}

inline bool EmitsPreparedSource(const LightSource& light)
{
    return !light.emitter || light.emitter->IsSurface() || light.emitter->HasVolumeEmission();
}

// The previous order's lights a volume target gathers, its own bounced light scaled by its self share.
inline Sources VolumeSources(const Target& target, unsigned int order,
                             const std::vector<std::shared_ptr<LightSource>>& previous,
                             const std::vector<const LightSource*>& previousOriginal)
{
    Sources sources;
    for (size_t source = 0; source < previous.size(); ++source)
    {
        const MathColour scale = (order > 1 && previousOriginal[source] == target.light) ? target.self : MathColour(1.0);
        if (!previous[source]->Photon_Only && EmitsPreparedSource(*previous[source]) &&
            previous[source]->Media_Interaction && Receives(target.light->emitterContainer, previousOriginal[source]) &&
            !scale.IsZero())
        {
            sources.lights.push_back(previous[source].get());
            sources.scales.push_back(scale);
        }
    }
    return sources;
}

inline std::shared_ptr<LightSource> Clone(const LightSource* light)
{
    return std::shared_ptr<LightSource>(
        static_cast<LightSource*>(Copy_Object(const_cast<LightSource*>(light))),
        [](LightSource* object) { Destroy_Object(object); });
}
} // namespace bounce_prepare

inline void PrepareVolumeBounceLightFields(TraceThreadData* td, const std::function<void()>& cooperate)
{
    using namespace bounce_prepare;
    const auto scene = std::const_pointer_cast<SceneData>(td->GetSceneData());
    if (std::none_of(scene->mediaLights.begin(), scene->mediaLights.end(),
                     [&](const LightSource* light) { return BounceSettings(*light) != nullptr || InternalPrimary(*light, *scene); }))
        return;
    if (td->preparationProgress)
        td->preparationProgress->Begin("bounce", BounceUnits(*scene));
    Workers workers(scene, td);
    std::vector<ConstObjectPtr> objects;
    std::unordered_set<ConstObjectPtr> seen;
    for (const auto object : scene->objects)
        CollectObjects(object, objects, seen);
    std::vector<Target> targets;
    std::vector<surface_refine::Part> parts;
    unsigned int maximum = 0;
    for (std::size_t lightIndex = 0; lightIndex < scene->mediaLights.size(); ++lightIndex)
    {
        LightSource* const light = scene->mediaLights[lightIndex];
        const auto owner = light->emitterContainer;
        const auto settings = BounceSettings(*light);
        const bool internalPrimary = InternalPrimary(*light, *scene);
        if (!settings && !internalPrimary)
            continue;
        const bool surface = light->emitter->IsSurface();
        Target target;
        target.light = light;
        target.primary = light->emitter;
        target.passes = settings ? settings->bouncePasses : 0;
        target.gain = settings ? settings->BounceGain() : 1.0;
        target.self = settings ? settings->bounceSelf : MathColour(1.0);
        target.internalPrimary = internalPrimary;
        maximum = std::max(maximum, target.passes);
        if (surface)
        {
            surface_refine::Part part;
            part.owner = const_cast<ObjectPtr>(owner);
            part.budget = std::size_t(light->sourceDomainCells > 0.0 ? light->sourceDomainCells : scene->surfaceDomainCells);
            part.fields = std::uint64_t(target.passes) + unsigned(internalPrimary);
            part.samples = unsigned(std::max(1, owner->surfaceLight->samples));
            part.sampler = std::make_shared<const SurfaceGeometrySampler>(part.owner);
            if (!part.sampler->IsValid())
                throw std::invalid_argument("Surface bounce requires finite surface geometry on " +
                    std::string(owner->source ? *owner->source : "an unnamed object"));
            part.sampler->ResolveDeferred();
            part.classes = settings ? SurfaceDiffuseClasses(owner) : std::vector<double>{1.0};
            for (const auto object : scene->objects)
                if (object->IsOpaque() && !Test_Flag(object, NO_SHADOW_FLAG))
                    part.cover.solids.push_back(object);
            part.cover.owner = part.owner;
            part.cover.radius = part.sampler->Radius();
            target.surface = true;
            target.part = parts.size();
            parts.push_back(std::move(part));
            targets.push_back(std::move(target));
            continue;
        }
        if (!owner->BBox.finiteInterior || Test_Flag(owner, INVERTED_FLAG))
            throw std::runtime_error("Bounce preparation requires a finite volume");
        if (!light->emitter->PreparedAxis())
            throw std::runtime_error("Bounce preparation requires a prepared primary field");
        target.domain = light->emitter->PreparedAxis()->Domain();
        if (!target.domain)
            throw std::runtime_error("Bounce preparation requires source coordinates");
        PrepareCoefficients(target, workers, objects, cooperate);
        maximum = std::max(maximum, target.passes);
        targets.push_back(std::move(target));
    }
    if (targets.empty())
    {
        if (td->preparationProgress)
            td->preparationProgress->End();
        return;
    }
    // Reused seed positions count once; refinement positions accumulate across all fields.
    std::uint64_t totalSurfaceNodes = 0;
    if (!parts.empty())
    {
        workers.Step("surface seeds");
        surface_refine::Seed(parts, workers, cooperate);
        for (const auto& part : parts)
            totalSurfaceNodes += part.seeds.size();
        AdvancePreparation(td->preparationProgress, parts.size());
    }
    std::vector<std::shared_ptr<LightSource>> previous;
    std::vector<const LightSource*> previousOriginal;
    for (LightSource* light : scene->lightSources)
    {
        previous.push_back(Clone(light));
        previousOriginal.push_back(light);
    }
    for (LightSource* light : scene->lightGroupLightSources)
    {
        previous.push_back(Clone(light));
        previousOriginal.push_back(light);
    }
    std::vector<std::size_t> primaryParts, primaryTargets;
    std::vector<Sources> primarySources;
    for (std::size_t t = 0; t < targets.size(); ++t)
        if (targets[t].internalPrimary)
        {
            primaryParts.push_back(targets[t].part);
            primaryTargets.push_back(t);
            primarySources.emplace_back();
            for (std::size_t source = 0; source < previous.size(); ++source)
                if (!previous[source]->Photon_Only && previous[source]->emitter &&
                    previous[source]->emitter->HasVolumeEmission() &&
                    Receives(targets[t].light->emitterContainer, previousOriginal[source]))
                    primarySources.back().lights.push_back(previous[source].get());
        }
    if (!primaryParts.empty())
    {
        const SubsurfaceScope scope(workers, scene);
        Trace::SurfaceSubsurfaceField bridge;
        std::vector<std::unique_ptr<Trace>> traces;
        std::vector<Trace*> entries;
        for (std::size_t worker = 0; worker < workers.Count(); ++worker)
        {
            Context& context = workers[worker];
            traces.emplace_back(new Trace(scene, workers.Thread(worker), QualityFlags(9), context.cooperate, context.media,
                                          context.radiosity));
            traces.back()->BeginSurfaceSubsurface(bridge);
            entries.push_back(traces.back().get());
        }
        surface_refine::Order<Workers> refine(workers, parts, primaryParts, primarySources, 0,
                                              entries, cooperate, kSurfaceUnits * primaryParts.size());
        const auto results = refine.Run("internal emission");
        totalSurfaceNodes += refine.NodeCount();
        for (std::size_t a = 0; a < primaryParts.size(); ++a)
        {
            totalSurfaceNodes -= parts[primaryParts[a]].seeds.size();
            Target& target = targets[primaryTargets[a]];
            workers.Step("internal emission " + Of("t", a, primaryParts.size()));
            const auto field = PrepareSubsurfaceBounce(workers, scene, results[a], primarySources[a], cooperate,
                                                       kSurfaceUnits, parts[primaryParts[a]].samples, false);
            target.primary = MakeBouncedSurfaceEmitter(target.primary, const_cast<ObjectPtr>(target.light->emitterContainer),
                                                       field, true);
            target.light->emitter = target.primary;
            target.light->colour = target.primary->Intensity() / M_PI;
        }
        for (std::size_t source = 0; source < previous.size(); ++source)
            previous[source] = Clone(previousOriginal[source]);
    }
    for (unsigned int index = 0; index < maximum; ++index)
    {
        const unsigned int order = index + 1;
        const std::string orderItem = Of("o", index, maximum);
        std::vector<std::shared_ptr<LightSource>> next;
        std::vector<const LightSource*> nextOriginal;
        struct SharedGroup
        {
            std::shared_ptr<const PreparedSourceDomain> domain;
            Sources sources;
            unsigned int draws;
            std::vector<PreparedBouncePhase> phases;
            std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> fields;
            std::shared_ptr<const PreparedSourceDistribution> proposal;
        };
        std::vector<SharedGroup> groups;
        std::vector<std::size_t> active, activeTarget(targets.size(), SIZE_MAX);
        std::vector<double> activeGain;
        std::vector<char> activeInternalPrimary;
        std::vector<Sources> activeSources;
        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
        {
            const Target& target = targets[targetIndex];
            if (!target.surface || order > target.passes)
                continue;
            // Its own bounced light is gathered at its self share; a zero share keeps its place so others draw alike.
            Sources sources;
            const bool scaled = !(target.self - MathColour(1.0)).IsZero();
            for (size_t source = 0; source < previous.size(); ++source)
                if (!previous[source]->Photon_Only && EmitsPreparedSource(*previous[source]) &&
                    Receives(target.light->emitterContainer, previousOriginal[source]))
                {
                    sources.lights.push_back(previous[source].get());
                    if (scaled)
                        sources.scales.push_back(order > 1 && previousOriginal[source] == target.light ? target.self : MathColour(1.0));
                }
            activeTarget[targetIndex] = active.size();
            active.push_back(target.part);
            activeGain.push_back(target.gain);
            activeInternalPrimary.push_back(target.internalPrimary);
            activeSources.push_back(std::move(sources));
        }
        std::vector<std::shared_ptr<const SurfaceBounceRadiance>> surfaceFields(active.size());
        std::vector<std::size_t> levelCounts;
        std::size_t reasons[3] = {0, 0, 0};
        if (!active.empty())
        {
            const auto start = std::chrono::steady_clock::now();
            std::vector<surface_refine::Result> results;
            std::size_t nodeCount = 0, bytes = 0;
            unsigned levels = 0;
            bool bound = false;
            {
                std::unique_ptr<SubsurfaceScope> scope(scene->useSubsurface ? new SubsurfaceScope(workers, scene) : nullptr);
                // Bridge traces read no scene lights, which preparation threads do not hold.
                Trace::SurfaceSubsurfaceField bridge;
                std::vector<std::unique_ptr<Trace>> traces;
                std::vector<Trace*> entries;
                for (std::size_t worker = 0; scene->useSubsurface && worker < workers.Count(); ++worker)
                {
                    Context& context = workers[worker];
                    traces.emplace_back(new Trace(scene, workers.Thread(worker), QualityFlags(9), context.cooperate, context.media,
                                                  context.radiosity));
                    traces.back()->BeginSurfaceSubsurface(bridge);
                    entries.push_back(traces.back().get());
                }
                surface_refine::Order<Workers> refine(workers, parts, active, activeSources, order, entries,
                                                      cooperate, kSurfaceUnits * active.size());
                results = refine.Run(orderItem);
                nodeCount = refine.NodeCount();
                totalSurfaceNodes += nodeCount;
                bytes = refine.Bytes();
                levels = refine.Levels();
                bound = refine.Bound();
                levelCounts = refine.LevelCounts();
                std::copy(refine.Reasons(), refine.Reasons() + 3, reasons);
            }
            const auto bridged = std::chrono::steady_clock::now();
            for (std::size_t a = 0; a < active.size(); ++a)
            {
                totalSurfaceNodes -= parts[active[a]].seeds.size();
                std::shared_ptr<const SurfaceBounceRadiance> subsurface;
                if (scene->useSubsurface)
                {
                    workers.Step(orderItem + " " + Of("t", a, active.size()) + " subsurface");
                    const Sources noSources;
                    subsurface = PrepareSubsurfaceBounce(workers, scene, results[a],
                        order == 1 && activeInternalPrimary[a] ? noSources : activeSources[a], cooperate, kSurfaceUnits, parts[active[a]].samples);
                }
                surfaceFields[a] = std::make_shared<const SumSurfaceBounceRadiance>(
                    std::vector<std::shared_ptr<const SurfaceBounceRadiance>>{results[a].diffuse, subsurface}, activeGain[a]);
                AdvancePreparation(td->preparationProgress);
            }
            if (td->preparationProgress)
            {
                char line[128];
                const auto now = std::chrono::steady_clock::now();
                std::snprintf(line, sizeof(line), "Bounce o%u: %lu nodes, %u levels, %.2f MiB, %.1f+%.1fs%s", order,
                              static_cast<unsigned long>(nodeCount), levels, double(bytes) / (1024 * 1024),
                              std::chrono::duration<double>(bridged - start).count(),
                              std::chrono::duration<double>(now - bridged).count(),
                              bound ? ", bound" : "");
                td->preparationProgress->Note(line);
                std::string text = "Bounce o" + std::to_string(order) + " levels:";
                for (std::size_t count : levelCounts)
                    text += " " + std::to_string(count);
                td->preparationProgress->Note(text);
                td->preparationProgress->Note("Bounce o" + std::to_string(order) + " splits: between " +
                                              std::to_string(reasons[0]) + ", within " + std::to_string(reasons[1]) +
                                              ", near " + std::to_string(reasons[2]));
            }
        }
        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
        {
            Target& target = targets[targetIndex];
            if (order > target.passes)
                continue;
            const auto owner = target.light->emitterContainer;
            if (target.surface)
            {
                const auto field = surfaceFields[activeTarget[targetIndex]];
                target.surfaceOrders.push_back(field);
                auto light = Clone(target.light);
                light->emitter =
                    MakeBouncedSurfaceEmitter(target.primary, const_cast<ObjectPtr>(owner), field, false);
                light->colour = light->emitter->Intensity() / M_PI;
                next.push_back(std::move(light));
                nextOriginal.push_back(target.light);
                continue;
            }
            const Sources sources = VolumeSources(target, order, previous, previousOriginal);
            const unsigned int draws = BounceGatherDraws(owner);
            auto group = std::find_if(groups.begin(), groups.end(),
                                      [&](const SharedGroup& candidate)
                                      {
                                          return candidate.domain == target.domain &&
                                                 candidate.draws == draws && candidate.sources == sources;
                                      });
            if (group != groups.end())
                AdvancePreparation(td->preparationProgress, target.domain->NodeCount() + target.domain->CellCount());
            if (group == groups.end())
            {
                SharedGroup prepared{target.domain, sources, draws, {}, {}, nullptr};
                for (const auto& candidate : targets)
                    if (!candidate.surface && candidate.domain == target.domain)
                        for (const auto& phase : candidate.phases)
                            if (std::find(prepared.phases.begin(), prepared.phases.end(), phase) ==
                                prepared.phases.end())
                                prepared.phases.push_back(phase);
                // Every phase scatters the same arriving light, so the nodes hold it once.
                auto moments = std::make_shared<std::vector<VolumeIncident>>(target.domain->NodeCount());
                const size_t groupIndex = groups.size();
                const std::string step = orderItem + " " + Of("t", targetIndex, targets.size());
                // A node gathers over a box of its domain cell.
                Vector3d cellSize;
                for (int axis = X; axis <= Z; ++axis)
                    cellSize[axis] = target.domain->Size()[axis] / double(target.domain->Counts()[axis]);
                VolumeGather gather;
                gather.lights.assign(sources.lights.begin(), sources.lights.end());
                gather.scales = sources.scales;
                gather.draws = draws;
                for (const auto& candidate : targets)
                    if (!candidate.surface && candidate.domain == target.domain)
                        gather.receivers.push_back(candidate.light->emitterContainer);
                workers.Step(step + " volume");
                workers.For(target.domain->NodeCount(), cooperate, [&](size_t node, size_t worker)
                {
                    // Nodes no scattering reaches through the stencil hold no light, as every coefficient there is zero.
                    const Vector3d at = target.domain->Node(node);
                    bool scatters = false;
                    for (const auto container : gather.receivers)
                        for (const Media& medium : container->interior->media)
                            scatters = scatters || medium.ScattersNear(container, at, cellSize);
                    if (!scatters)
                        return;
                    TraceTicket ticket(256, 0.0);
                    thread_local std::vector<IncidentDraw> draws;
                    thread_local std::vector<int> scratch;
                    (*moments)[node] = GatherVolumeIncident(workers[worker].trace, at, cellSize, gather,
                                                            BounceOrderSeed(0, order, groupIndex, node), ticket, draws, scratch);
                });
                for (size_t phase = 0; phase < prepared.phases.size(); ++phase)
                    prepared.fields.push_back(std::make_shared<const BounceMomentGrid>(
                        target.domain, moments, prepared.phases[phase].type, prepared.phases[phase].eccentricity, phase == 0));
                std::vector<const Target*> contributing;
                for (const auto& candidate : targets)
                    if (!candidate.surface && candidate.domain == target.domain &&
                        order <= candidate.passes &&
                        BounceGatherDraws(candidate.light->emitterContainer) == draws &&
                        VolumeSources(candidate, order, previous, previousOriginal) == sources)
                        contributing.push_back(&candidate);
                std::vector<DBL> proposalMasses(target.domain->CellCount());
                workers.Step(step + " proposal");
                workers.For(proposalMasses.size(), cooperate, [&](size_t cell, size_t worker)
                {
                    const auto point = target.domain->CellMidpoint(cell);
                    MathColour value;
                    for (const auto candidate : contributing)
                        if (Inside_Object(point, const_cast<ObjectPtr>(candidate->light->emitterContainer),
                                          workers.Thread(worker)))
                            for (size_t phase = 0; phase < candidate->phases.size(); ++phase)
                            {
                                const size_t index = std::find(prepared.phases.begin(), prepared.phases.end(),
                                                               candidate->phases[phase]) -
                                                     prepared.phases.begin();
                                value += candidate->coefficients[phase]->Sample(point) *
                                         prepared.fields[index]->AngularAverage(point);
                            }
                    proposalMasses[cell] = value.Weight() * target.domain->CellVolume(cell);
                });
                prepared.proposal =
                    std::make_shared<const PreparedSourceDistribution>(target.domain, proposalMasses);
                if (td->mediaMessages)
                {
                    size_t bytes = prepared.proposal->StorageBytes();
                    for (const auto& phase : prepared.fields)
                        bytes += phase->StorageBytes();
                    td->mediaMessages->Info("Prepared shared volume bounce order %u: %u nodes; %u phase "
                                            "responses; %.3f MiB response and proposal storage.",
                                            order, unsigned(target.domain->NodeCount()),
                                            unsigned(prepared.phases.size()), double(bytes) / (1024 * 1024));
                }
                groups.push_back(std::move(prepared));
                group = groups.end() - 1;
            }
            std::vector<PreparedOwnerBounceField::Component> components;
            for (size_t phase = 0; phase < target.phases.size(); ++phase)
            {
                const size_t index =
                    std::find(group->phases.begin(), group->phases.end(), target.phases[phase]) -
                    group->phases.begin();
                components.push_back({group->fields[index], target.coefficients[phase]});
            }
            const auto field =
                std::make_shared<const PreparedOwnerBounceField>(target.domain, std::move(components));
            target.orders.push_back(field);
            target.proposal = group->proposal;
            auto light = Clone(target.light);
            light->emitter = MakeBouncedInteriorEmitter(*target.primary, field, td, false, target.proposal);
            light->colour = light->emitter->Intensity() / M_PI;
            next.push_back(std::move(light));
            nextOriginal.push_back(target.light);
        }
        previous.swap(next);
        previousOriginal.swap(nextOriginal);
    }
    struct FinalProposal
    {
        std::shared_ptr<const PreparedSourceDomain> domain;
        std::shared_ptr<const PreparedSourceDistribution> proposal;
    };
    std::vector<FinalProposal> finalProposals;
    for (const auto& target : targets)
        if (!target.surface &&
            std::none_of(finalProposals.begin(), finalProposals.end(),
                         [&](const FinalProposal& candidate) { return candidate.domain == target.domain; }))
        {
            std::vector<DBL> masses(target.domain->CellCount());
            workers.Step("final proposal");
            workers.For(masses.size(), cooperate, [&](size_t cell, size_t worker)
            {
                const auto point = target.domain->CellMidpoint(cell);
                MathColour value;
                for (const auto& candidate : targets)
                    if (!candidate.surface && candidate.domain == target.domain &&
                        Inside_Object(point, const_cast<ObjectPtr>(candidate.light->emitterContainer),
                                      workers.Thread(worker)))
                    {
                        value += candidate.primary->PreparedAxis()->ColourAt(point);
                        for (const auto& order : candidate.orders)
                            value += order->AngularAverage(point);
                    }
                masses[cell] = value.Weight() * target.domain->CellVolume(cell);
            });
            finalProposals.push_back(
                {target.domain, std::make_shared<const PreparedSourceDistribution>(target.domain, masses)});
        }
    for (Target& target : targets)
    {
        if (target.surface)
        {
            const auto owner = const_cast<ObjectPtr>(target.light->emitterContainer);
            const auto field =
                std::make_shared<const SumSurfaceBounceRadiance>(std::move(target.surfaceOrders));
            // Its own surface receives the light as it was before bouncing and the self share of the bounce.
            if (!(target.self - MathColour(1.0)).IsZero())
            {
                if (!target.primary->Intensity().IsZero() || !target.self.IsZero())
                {
                    target.light->ownLight = Clone(target.light);
                    target.light->ownLight->emitter = MakeBouncedSurfaceEmitter(
                        target.primary, owner, std::make_shared<const TintedSurfaceBounceRadiance>(field, target.self), true);
                    target.light->ownLight->colour = target.light->ownLight->emitter->Intensity() / M_PI;
                    target.light->ownLight->Center = target.light->ownLight->emitter->Centre();
                    target.light->ownLight->lawRadius = target.light->ownLight->emitter->Radius();
                }
                target.light->ownSurface = owner;
            }
            target.light->emitter = MakeBouncedSurfaceEmitter(target.primary, owner, field, true);
        }
        else
        {
            // Its own media scatter the self share of the bounced light, and the rest of the light as it was before bouncing.
            if (!(target.self - MathColour(1.0)).IsZero())
            {
                const auto owner = target.light->emitterContainer;
                if (target.primary->HasVolumeEmission())
                {
                    target.light->selfLight = Clone(target.light);
                    target.light->selfLight->selfContainer = owner;
                    target.light->selfLight->selfScale = target.self;
                    target.light->selfLight->selfOnly = true;
                }
                target.light->selfContainer = owner;
                target.light->selfScale = target.self;
            }
            const auto field =
                std::make_shared<const SumPreparedBounceField>(target.domain, std::move(target.orders));
            target.light->emitter =
                MakeBouncedInteriorEmitter(*target.primary, field, td, true,
                                           std::find_if(finalProposals.begin(), finalProposals.end(),
                                                        [&](const FinalProposal& candidate)
                                                        { return candidate.domain == target.domain; })
                                               ->proposal);
        }
        target.light->colour = target.light->emitter->Intensity() / M_PI;
        target.light->Center = target.light->emitter->Centre();
        target.light->lawRadius = target.light->emitter->Radius();
        if (target.surface)
            continue;
        for (const auto& field : target.light->emitterContainer->interior->fields)
            if (field->owner == target.light->emitterContainer)
                for (auto& component : field->lights)
                    if (component.source == target.light)
                        component.payload = target.light->emitter->PreparedAxis();
    }
    if (td->preparationProgress)
    {
        if (!parts.empty())
            td->preparationProgress->Note("Surface light total: " + std::to_string(totalSurfaceNodes) +
                " nodes across " + std::to_string(parts.size()) + " lights.");
        td->preparationProgress->End();
    }
}

} // namespace pov

#endif
