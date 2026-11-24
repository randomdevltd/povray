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
#include "core/support/preparedcache.h"
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
// A node no light reached is one byte: most of a volume's nodes are empty.
inline void Serialize(CacheArchive& archive, VolumeIncident& incident)
{
    static_assert(sizeof(VolumeLobe) == 6 * sizeof(float), "lobes are written as packed floats");
    std::uint8_t form = incident.valid ? 1 : 0;
    if (!archive.Reading() && !incident.valid)
        for (const VolumeLobe& lobe : incident.lobes)
            for (int axis = 0; axis < 3; ++axis)
                if ((lobe.light[axis] != 0.0f) || (lobe.moment[axis] != 0.0f))
                    form = 2;
    Serialize(archive, form);
    if (form > 2)
        throw CacheFormatError("a volume node's form is unknown");
    incident.valid = (form == 1);
    if (form != 0)
        archive.Raw(incident.lobes.data(), sizeof(incident.lobes));
}

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

// An object as the scene wrote it, for messages.
inline std::string Describe(ConstObjectPtr object)
{
    return std::string(ShapeName(object)) + (object->source ? " at " + *object->source : std::string());
}

// The trace steps of the heaviest gather seen, and its object.
struct Heaviest final
{
    std::size_t steps = 0;
    ConstObjectPtr object = nullptr;
    void Merge(const Heaviest& other)
    {
        if (other.steps > steps)
            *this = other;
    }
};

struct Context final
{
    Context(std::shared_ptr<SceneData> scene, TraceThreadData* td, const std::function<void()>& callback)
        : cooperate([this, callback]() { if (callback) callback(); ++steps; }), trace(scene, td, QualityFlags(9), cooperate, media, radiosity),
          media(td, &trace, &photons),
          photons(&scene->GetPreparedSet(0).surfacePhotonMap, scene->photonSettings)
    {
    }
    // Names the object this worker gathers on, for the stall line, and counts its trace steps from here.
    void Watch(ConstObjectPtr object)
    {
        owner.store(object, std::memory_order_relaxed);
        steps = 0;
    }
    void Done()
    {
        if (const ConstObjectPtr object = owner.load(std::memory_order_relaxed))
            heaviest.Merge({steps, object});
        owner.store(nullptr, std::memory_order_relaxed);
    }
    std::atomic<ConstObjectPtr> owner{nullptr};
    std::size_t steps = 0;
    Heaviest heaviest;
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
        Run(count, cooperate, function, units);
    }
    // As For, outside the progress count, for work whose amount depends on the thread count.
    template<typename Function>
    void Uncounted(std::size_t count, const std::function<void()>& cooperate, const Function& function)
    {
        Run(count, cooperate, function, 0);
    }
    template<typename Function>
    void Run(std::size_t count, const std::function<void()>& cooperate, const Function& function, std::size_t units)
    {
        const auto advance = [count, units](std::size_t index) { return (index + 1) * units / count - index * units / count; };
        try
        {
            const Watching watching(*this);
            ParallelFor(count, contexts.size(), [&](std::size_t index, std::size_t worker)
            {
                stop();
                function(index, worker);
                contexts[worker]->Done();
                if (units > 0)
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
    // The heaviest gather since the last call, over every worker.
    Heaviest TakeHeaviest()
    {
        Heaviest result;
        for (const auto& context : contexts)
        {
            result.Merge(context->heaviest);
            context->heaviest = Heaviest();
        }
        return result;
    }
    // The objects the workers gather on, which a stalled progress line names.
    std::string Gathering() const
    {
        std::vector<ConstObjectPtr> objects;
        for (const auto& context : contexts)
            if (const ConstObjectPtr object = context->owner.load(std::memory_order_relaxed))
                if (std::find(objects.begin(), objects.end(), object) == objects.end())
                    objects.push_back(object);
        std::string text;
        for (const ConstObjectPtr object : objects)
            text += (text.empty() ? "gathering on " : ", ") + Describe(object);
        return text;
    }
    struct Watching final
    {
        explicit Watching(const Workers& workers) : progress(workers.td->preparationProgress)
        {
            if (progress)
                progress->Watch([&workers]() { return workers.Gathering(); });
        }
        ~Watching()
        {
            if (progress)
                progress->Watch(nullptr);
        }
        PreparationProgress* progress;
    };
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
    std::size_t part = 0;
};

// Progress units of a surface target's placement, however many points it shades.
constexpr std::size_t kSurfaceUnits = 4096;

inline std::string Of(const char* name, std::size_t index, std::size_t count)
{
    return name + std::to_string(index + 1) + "/" + std::to_string(count);
}

// A placed light's points and what they export; the node index is rebuilt from the points when read back.
inline void SerializePlaced(CacheArchive& archive, surface_refine::Result& result)
{
    surface_refine::Layout& layout = result.layout;
    Serialize(archive, layout.points);
    Serialize(archive, layout.normals);
    Serialize(archive, layout.spacings);
    Serialize(archive, layout.areas);
    Serialize(archive, layout.front);
    Serialize(archive, layout.back);
    Serialize(archive, layout.ids);
    Serialize(archive, result.levels);
    if (!archive.Reading())
        return;
    const std::size_t count = layout.points.size();
    if ((layout.normals.size() != count) || (layout.spacings.size() != count) || (layout.areas.size() != count) ||
        (layout.front.size() != count) || (layout.back.size() != count) || (layout.ids.size() != count))
        throw CacheFormatError("a bounce light's point arrays differ in length");
    result.points = count;
    result.index = count ? std::make_shared<const SurfaceNodeIndex>(layout.points, layout.normals, layout.spacings) : nullptr;
}

// A loaded step still moves the progress line as far as building it would have.
inline void AdvanceLoaded(const PreparedCacheUnit& cache, TraceThreadData* td, std::size_t units)
{
    if (cache.Loading())
        AdvancePreparation(td->preparationProgress, units);
}

inline bool Receives(const SceneData& scene, ConstObjectPtr owner, const LightSource* light)
{
    if (!light->lightGroupLight ? Test_Flag(owner, NO_GLOBAL_LIGHTS_FLAG) :
        (std::find(owner->LLights.begin(), owner->LLights.end(), light) == owner->LLights.end()))
        return false;
    // a scattering medium of the owner's interior whose lit_by_tags selects the light
    if (owner->interior == nullptr)
        return true;
    bool scattering = false;
    for (const Media& medium : owner->interior->media)
        if (medium.use_scattering)
        {
            if (scene.LitBy(medium.litBy, *light))
                return true;
            scattering = true;
        }
    return !scattering;
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
    return scene.useSubsurface && light.emitter && light.emitter->IsSurface() && owner && owner->surfaceLight &&
        owner->surfaceLight->useEmission && (owner->surfaceLight->bouncePasses == 0) && Method2Host(owner, scene) &&
        std::any_of(scene.mediaLights.begin(), scene.mediaLights.end(), [&](const LightSource* source)
            { return !source->Photon_Only && source->emitter && !source->emitter->IsSurface() &&
                     source->emitter->HasVolumeEmission() && source->emitterContainer &&
                     BoxesMeet(source->emitterContainer->BBox, owner->BBox); });
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
                                const std::function<void()>& cooperate, PreparedCacheUnit& cache)
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
    cache.Record("coefficients", [&]()
    {
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
    }, [&](CacheArchive& archive)
    {
        std::uint64_t phases = values.size();
        Serialize(archive, phases);
        if (phases != target.phases.size())
            throw CacheFormatError("a bounce light's coefficients do not match its phases");
    });
    for (auto& phase : values)
        cache.Grid("coefficient nodes", phase, constant ? std::array<std::size_t, 3>{{ 1, 1, 1 }} : target.domain->NodeCounts(),
                   LatticeBounds(target.domain->Low(), target.domain->CellSize(), true));
    AdvanceLoaded(cache, workers.td, target.domain->NodeCount());
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
    SubsurfaceScope(Workers& workers, const std::shared_ptr<SceneData>& scene) : workers(workers), memory(scene->subsurfaceMemory)
    {
        for (std::size_t worker = 0; worker < workers.Count(); ++worker)
            saved.push_back(workers.Thread(worker)->subsurfaceCache);
        Renew();
    }
    /// A fresh cache for lights that have changed: a cell keeps the lights it was lit by.
    void Renew()
    {
        const auto cache = std::make_shared<SubsurfaceCache>(memory);
        for (std::size_t worker = 0; worker < workers.Count(); ++worker)
            workers.Thread(worker)->subsurfaceCache = cache;
    }
    ~SubsurfaceScope()
    {
        for (std::size_t worker = 0; worker < saved.size(); ++worker)
            workers.Thread(worker)->subsurfaceCache = saved[worker];
    }
    Workers& workers;
    double memory;
    std::vector<std::shared_ptr<SubsurfaceCache>> saved;
};

// Gives every worker its own copies of the scene's lights while it lasts, as a render thread holds them.
struct PassLights final
{
    PassLights(Workers& workers, const SceneData& scene) : workers(workers)
    {
        for (std::size_t worker = 0; worker < workers.Count(); ++worker)
        {
            std::vector<LightSource*> lights;
            for (LightSource* light : scene.lightSources)
            {
                LightSource* const copy = static_cast<LightSource*>(Copy_Object(light));
                if (copy->Photon_Only)
                {
                    copy->colour.Clear();
                    copy->Media_Interaction = false;
                }
                lights.push_back(copy);
            }
            saved.push_back(std::move(workers.Thread(worker)->lightSources));
            workers.Thread(worker)->lightSources = std::move(lights);
        }
    }
    ~PassLights()
    {
        for (std::size_t worker = 0; worker < saved.size(); ++worker)
        {
            for (LightSource* light : workers.Thread(worker)->lightSources)
                Destroy_Object(light);
            workers.Thread(worker)->lightSources = std::move(saved[worker]);
        }
    }
    Workers& workers;
    std::vector<std::vector<LightSource*>> saved;
};

// Bounce progress units, as the passes below count them; zero while a volume target's domain is unprepared.
inline std::size_t BounceUnits(const SceneData& scene)
{
    std::size_t units = 0;
    std::vector<std::shared_ptr<const PreparedSourceDomain>> domains;
    for (const LightSource* light : scene.mediaLights)
    {
        const MediaLight* const settings = BounceSettings(*light);
        if (!settings && InternalPrimary(*light, scene))
            units += kSurfaceUnits + 1;
        if (!settings)
            continue;
        if (light->emitter->IsSurface())
        {
            units += kSurfaceUnits + 1;
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
inline Sources VolumeSources(const SceneData& scene, const Target& target, unsigned int order,
                             const std::vector<std::shared_ptr<LightSource>>& previous,
                             const std::vector<const LightSource*>& previousOriginal)
{
    Sources sources;
    for (size_t source = 0; source < previous.size(); ++source)
    {
        const MathColour scale = (order > 1 && previousOriginal[source] == target.light) ? target.self : MathColour(1.0);
        if (!previous[source]->Photon_Only && EmitsPreparedSource(*previous[source]) &&
            previous[source]->Media_Interaction && Receives(scene, target.light->emitterContainer, previousOriginal[source]) &&
            !scale.IsZero())
        {
            sources.lights.push_back(previous[source].get());
            sources.scales.push_back(scale);
        }
    }
    return sources;
}

// A placement's report is saved with its cache; a loaded one, look-ahead counts included, is the building run's.
inline void NoteReport(PreparedCacheUnit& cache, const char* label, std::string& report)
{
    cache.Note(label, report);
    if (cache.Loading())
        report = "from the run that built the cache: " + report;
}

inline std::shared_ptr<LightSource> Clone(const LightSource* light)
{
    return std::shared_ptr<LightSource>(
        static_cast<LightSource*>(Copy_Object(const_cast<LightSource*>(light))),
        [](LightSource* object) { Destroy_Object(object); });
}
// The heaviest gather since the last report, so an unusually slow object is named once its work is done.
inline void ReportHeaviest(Workers& workers, const std::string& what)
{
    const Heaviest heaviest = workers.TakeHeaviest();
    if ((workers.td->preparationProgress != nullptr) && (heaviest.object != nullptr))
        workers.td->preparationProgress->Note("Bounce " + what + " heaviest gather: " + std::to_string(heaviest.steps) +
                                              " trace steps on " + Describe(heaviest.object), true);
}

// A light's own `self` share when it does not bounce: its own object, or its own media, receive only that share of it.
inline void ApplySelf(LightSource& light)
{
    const auto owner = light.emitterContainer;
    if (!owner || !light.emitter)
        return;
    const MediaLight* settings = light.emitter->IsSurface() ? owner->surfaceLight.get()
                                                            : (owner->interior ? owner->interior->light.get() : nullptr);
    if (!settings || (settings->bouncePasses > 0) || (settings->selfShare - MathColour(1.0)).IsZero())
        return;
    if (!light.emitter->IsSurface())
    {
        light.selfContainer = owner;
        light.selfScale = settings->selfShare;
        return;
    }
    light.ownSurface = owner;
    light.ownLight = nullptr;
    if (settings->selfShare.IsZero())
        return;
    light.ownLight = Clone(&light);
    light.ownLight->ownSurface = nullptr;
    light.ownLight->ownLight = nullptr;
    light.ownLight->emitter = std::make_shared<const TintedSurfaceEmitter>(light.emitter, settings->selfShare);
    light.ownLight->colour = light.ownLight->emitter->Intensity() / M_PI;
}

// Gives a surface bounce light a field: what it emits to everything else, and to its own object only its self share.
inline void Feed(LightSource& light, const std::shared_ptr<const Emitter>& primary, ObjectPtr owner,
                 const std::shared_ptr<const SurfaceBounceRadiance>& field, const MathColour& self)
{
    light.emitter = MakeBouncedSurfaceEmitter(primary, owner, field, true);
    light.colour = light.emitter->Intensity() / M_PI;
    light.Center = light.emitter->Centre();
    light.lawRadius = light.emitter->Radius();
    light.ownSurface = nullptr;
    light.ownLight = nullptr;
    if ((self - MathColour(1.0)).IsZero())
        return;
    light.ownSurface = owner;
    if (self.IsZero() && primary->Intensity().IsZero())
        return;
    light.ownLight = Clone(&light);
    light.ownLight->ownSurface = nullptr;
    light.ownLight->ownLight = nullptr;
    light.ownLight->emitter = MakeBouncedSurfaceEmitter(primary, owner, std::make_shared<const TintedSurfaceBounceRadiance>(field, self), true);
    light.ownLight->colour = light.ownLight->emitter->Intensity() / M_PI;
}

inline void FeedVolume(const Target& target, TraceThreadData* td, PreparedCacheUnit& cache)
{
    LightSource& light = *target.light;
    const auto owner = target.light->emitterContainer;
    light.selfLight = nullptr;
    if (!(target.self - MathColour(1.0)).IsZero() && target.primary->HasVolumeEmission())
    {
        light.selfLight = Clone(&light);
        light.selfLight->emitter = target.primary;
        light.selfLight->colour = target.primary->Intensity() / M_PI;
        light.selfLight->selfContainer = owner;
        light.selfLight->selfScale = target.self;
        light.selfLight->selfOnly = true;
    }
    const auto field = std::make_shared<const SumPreparedBounceField>(target.domain, target.orders);
    light.emitter = MakeBouncedInteriorEmitter(*target.primary, field, td, true, target.proposal, &cache);
    light.colour = light.emitter->Intensity() / M_PI;
    light.Center = light.emitter->Centre();
    light.lawRadius = light.emitter->Radius();
    light.selfContainer = owner;
    light.selfScale = target.self;
    light.bounceFed = true;
    for (const auto& field : owner->interior->fields)
        if (field->owner == owner)
            for (auto& component : field->lights)
                if (component.source == target.light)
                    component.payload = light.emitter->PreparedAxis();
}

// The factor taking a pass's field to the light's stated brightness: its plain multiplier, or the share making its total
// (lumens: power, pi times the integral; nits: the mean radiance, integral over area) the stated amount. Zero when a unit
// is stated over a field that received no light.
inline double PassScale(const MediaLight& settings, const MathColour& integral, double area)
{
    if (settings.lumens >= 0.0)
    {
        const double power = M_PI * integral.Greyscale();
        return (settings.lumens > 0.0 && power > 0.0) ? settings.lumens / power : 0.0;
    }
    if (settings.nits)
    {
        const double mean = area > 0.0 ? integral.Greyscale() / area : 0.0;
        return (settings.brightness > 0.0 && mean > 0.0) ? settings.brightness / mean : 0.0;
    }
    return settings.brightness;
}

// Gives each emitting surface on a method 2 host its buried volume emitters' escaping light as part of its primary, placed
// and shaded as a bounce light's first pass is but taking only those emitters' light; bounce lights then receive it.
inline void PrepareInternalPrimaries(Workers& workers, const std::shared_ptr<SceneData>& scene, TraceThreadData* td,
                                     const std::function<void()>& cooperate, PreparedCacheUnit& cache)
{
    for (LightSource* light : scene->mediaLights)
    {
        if (BounceSettings(*light) || !InternalPrimary(*light, *scene))
            continue;
        const auto owner = const_cast<ObjectPtr>(light->emitterContainer);
        const MediaLight& settings = *owner->surfaceLight;
        surface_refine::Part part;
        part.owner = owner;
        part.internal = true;
        part.budget = std::size_t(light->sourceDomainCells > 0.0 ? light->sourceDomainCells : scene->surfaceDomainCells);
        part.directions = std::max(1u, settings.bounceDirections);
        const SurfaceGeometrySampler geometry(owner);
        if (!geometry.IsValid())
        {
            AdvancePreparation(td->preparationProgress, kSurfaceUnits + 1);
            continue;
        }
        geometry.ResolveDeferred();
        part.map = scene->surfaceMaps->Of(owner);
        part.inside = (owner->Type & (PATCH_OBJECT | NO_INSIDE_OBJECT)) != 0;
        workers.Step("internal emission " + Describe(owner));
        surface_refine::Result result;
        cache.Record("internal", [&]()
        {
            const PassLights lights(workers, *scene);
            const SubsurfaceScope scope(workers, scene);
            std::vector<std::unique_ptr<Trace>> traces;
            std::vector<Trace*> shaders;
            for (std::size_t worker = 0; worker < workers.Count(); ++worker)
            {
                auto& context = workers[worker];
                traces.emplace_back(new Trace(scene, workers.Thread(worker), QualityFlags(9), context.cooperate, context.media,
                                              context.radiosity));
                shaders.push_back(traces.back().get());
            }
            result = surface_refine::Place(workers, part, shaders, cooperate, kSurfaceUnits);
        }, [&](CacheArchive& archive) { SerializePlaced(archive, result); });
        NoteReport(cache, "internal report", result.report);
        AdvanceLoaded(cache, td, kSurfaceUnits);
        AdvancePreparation(td->preparationProgress);
        if (result.points == 0)
        {
            if (td->preparationProgress)
                td->preparationProgress->Warn("Internal emission found no surface on the " + Describe(owner) +
                                              ": its buried lights are not exported. " + result.report);
            continue;
        }
        const auto field = surface_refine::Field(result, result.layout.front, result.layout.back);
        if (td->preparationProgress)
        {
            char line[160];
            std::snprintf(line, sizeof(line), "Internal emission: %lu points, power %.9g on the ",
                          static_cast<unsigned long>(result.points), M_PI * field->IntegratedRadiance().Greyscale());
            td->preparationProgress->Note(line + Describe(owner), true);
        }
        Feed(*light, light->emitter, owner, field, settings.selfShare);
    }
}
} // namespace bounce_prepare

inline void PrepareVolumeBounceLightFields(TraceThreadData* td, const std::function<void()>& cooperate)
{
    using namespace bounce_prepare;
    const auto scene = std::const_pointer_cast<SceneData>(td->GetSceneData());
    scene->surfaceMaps = std::make_shared<surface_map::Maps>();
    for (LightSource* light : scene->mediaLights)
        ApplySelf(*light);
    if (std::none_of(scene->mediaLights.begin(), scene->mediaLights.end(),
                     [&](const LightSource* light) { return BounceSettings(*light) != nullptr || InternalPrimary(*light, *scene); }))
        return;
    if (td->preparationProgress)
        td->preparationProgress->Begin("bounce", BounceUnits(*scene));
    // every costly step below is a record of one saved cache, read back in the same order when its key matches
    PreparedCacheUnit cache(scene->preparedCaches.get(), "bounce", CacheKey().Add(std::uint64_t(scene->mediaLights.size())).Value());
    Workers workers(scene, td);
    PrepareInternalPrimaries(workers, scene, td, cooperate, cache);
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
        if (!settings)
            continue;
        const bool surface = light->emitter->IsSurface();
        Target target;
        target.light = light;
        target.primary = light->emitter;
        target.passes = settings->bouncePasses;
        target.gain = settings->BounceGain();
        target.self = settings->selfShare;
        if (surface)
        {
            surface_refine::Part part;
            part.owner = const_cast<ObjectPtr>(owner);
            part.budget = std::size_t(light->sourceDomainCells > 0.0 ? light->sourceDomainCells : scene->surfaceDomainCells);
            part.directions = settings->bounceDirections;
            // a count of 0 would silently drop the view-dependent terms of the light the nodes export
            if (part.directions < 1)
                throw std::invalid_argument("Surface bounce view_directions must be at least 1 on " +
                    std::string(owner->source ? *owner->source : "an unnamed object"));
            PreparedSurfaceInput::ValidateBounceMaterial(owner, scene->useSubsurface);
            const SurfaceGeometrySampler geometry(part.owner);
            if (!geometry.IsValid())
            {
                // Nothing to bounce from: the light keeps what it had before bouncing, and the scene goes on.
                if (td->preparationProgress)
                {
                    td->preparationProgress->Warn("Surface bounce found no surface geometry on the " + Describe(owner) +
                                                  ": it has no finite surface of nonzero area; no bounce is applied.");
                    AdvancePreparation(td->preparationProgress, kSurfaceUnits + 1);
                }
                continue;
            }
            geometry.ResolveDeferred();
            part.keepHits = target.passes > 1;
            part.map = scene->surfaceMaps->Of(part.owner);
            part.inside = (owner->Type & (PATCH_OBJECT | NO_INSIDE_OBJECT)) || std::any_of(scene->lightSources.begin(), scene->lightSources.end(),
                [&](const LightSource* source) { return !BounceSettings(*source) && Inside_Object(source->Center, part.owner, td); });
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
        PrepareCoefficients(target, workers, objects, cooperate, cache);
        maximum = std::max(maximum, target.passes);
        targets.push_back(std::move(target));
    }
    if (targets.empty())
    {
        cache.Finish();
        if (td->preparationProgress)
            td->preparationProgress->End();
        return;
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
    // Surface passes replace an accumulated field; volume orders gather only the change since the preceding pass.
    std::size_t totalSurfaceNodes = 0;
    {
        const std::unique_ptr<PassLights> lights(parts.empty() ? nullptr : new PassLights(workers, *scene));
        // Its own subsurface cache, its cloud as fine as the material needs: one cache serves lights of different spacings.
        const std::unique_ptr<SubsurfaceScope> scope(parts.empty() ? nullptr : new SubsurfaceScope(workers, scene));
        std::vector<std::unique_ptr<Trace>> traces;
        std::vector<Trace*> shaders;
        const auto makeShaders = [&]()
        {
            traces.clear();
            shaders.clear();
            if (parts.empty())
                return;
            for (std::size_t worker = 0; worker < workers.Count(); ++worker)
            {
                Context& context = workers[worker];
                traces.emplace_back(new Trace(scene, workers.Thread(worker), QualityFlags(9), context.cooperate, context.media,
                                              context.radiosity));
                shaders.push_back(traces.back().get());
            }
        };
        makeShaders();
        std::vector<surface_refine::Result> placed(targets.size());
        std::vector<std::shared_ptr<const SurfaceBounceRadiance>> fields(targets.size());
        std::vector<double> areas(targets.size(), 0.0);
        unsigned int surfacePasses = 0;
        for (std::size_t t = 0; t < targets.size(); ++t)
        {
            Target& target = targets[t];
            if (!target.surface)
                continue;
            const auto start = std::chrono::steady_clock::now();
            const surface_refine::Part& part = parts[target.part];
            const auto owner = const_cast<ObjectPtr>(target.light->emitterContainer);
            workers.Step("o1 " + Describe(owner));
            cache.Record("o1", [&]() { placed[t] = surface_refine::Place(workers, part, shaders, cooperate, kSurfaceUnits); },
                         [&](CacheArchive& archive) { SerializePlaced(archive, placed[t]); });
            NoteReport(cache, "o1 report", placed[t].report);
            AdvanceLoaded(cache, td, kSurfaceUnits);
            const surface_refine::Result& result = placed[t];
            totalSurfaceNodes += result.points;
            AdvancePreparation(td->preparationProgress);
            if (result.points == 0)
            {
                // Nothing found: the light keeps what it had before bouncing, and the scene goes on.
                if (td->preparationProgress)
                    td->preparationProgress->Warn("Surface bounce found no surface on the " + Describe(owner) +
                                                  ": its surface map holds no point at any level read; no bounce is applied. " + result.report);
                continue;
            }
            for (double area : result.layout.areas)
                areas[t] += area;
            surfacePasses = std::max(surfacePasses, target.passes);
            if (td->preparationProgress)
            {
                char line[128];
                std::snprintf(line, sizeof(line), "Bounce o1: %lu nodes, %u levels, %.1fs", static_cast<unsigned long>(result.points),
                              unsigned(result.levels.size()),
                              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
                td->preparationProgress->Note(line);
                std::string text = "Bounce o1 levels:";
                for (std::size_t count : result.levels)
                    text += " " + std::to_string(count);
                td->preparationProgress->Note(text);
                td->preparationProgress->Note("Bounce o1 " + Describe(owner) + ": " + result.report, true);
            }
        }
        ReportHeaviest(workers, "o1");
        // Workers own global lights; grouped lights remain on the scene.
        std::vector<std::size_t> copyOf(scene->lightSources.size(), targets.size());
        for (std::size_t t = 0; t < targets.size(); ++t)
            for (std::size_t i = 0; i < scene->lightSources.size(); ++i)
                if (scene->lightSources[i] == targets[t].light)
                    copyOf[i] = t;
        maximum = std::max(maximum, surfacePasses);
        for (unsigned int index = 0; index < maximum; ++index)
        {
            const unsigned int pass = index + 1;
            const auto priorFields = fields;
            const std::string item = "o" + std::to_string(pass);
            std::vector<std::vector<MathColour>> fronts(targets.size()), backs(targets.size());
            if (pass > 1 && pass <= surfacePasses)
            {
                for (const Target& target : targets)
                    if (!target.surface && !target.orders.empty())
                        FeedVolume(target, td, cache);
                for (std::size_t worker = 0; worker < workers.Count(); ++worker)
                    for (std::size_t i = 0; i < copyOf.size(); ++i)
                        if (copyOf[i] < targets.size())
                        {
                            const Target& target = targets[copyOf[i]];
                            LightSource& copy = *workers.Thread(worker)->lightSources[i];
                            if (target.surface && fields[copyOf[i]])
                            {
                                Feed(copy, target.primary, const_cast<ObjectPtr>(target.light->emitterContainer), fields[copyOf[i]], target.self);
                                copy.bounceFed = true;
                            }
                            else if (!target.surface && !target.orders.empty())
                            {
                                copy.emitter = target.light->emitter;
                                copy.colour = target.light->colour;
                                copy.Center = target.light->Center;
                                copy.lawRadius = target.light->lawRadius;
                                copy.selfLight = target.light->selfLight;
                                copy.selfContainer = target.light->selfContainer;
                                copy.selfScale = target.light->selfScale;
                                copy.bounceFed = true;
                            }
                        }
                for (std::size_t t = 0; t < targets.size(); ++t)
                    if (fields[t] && targets[t].light->lightGroupLight)
                    {
                        Feed(*targets[t].light, targets[t].primary, const_cast<ObjectPtr>(targets[t].light->emitterContainer), fields[t],
                             targets[t].self);
                        targets[t].light->bounceFed = true;
                    }
                // the fed lights are new: a cloud, or a thread's subsurface parts, kept from the last pass would light by its lights
                traces.clear();
                scope->Renew();
                makeShaders();
                for (std::size_t t = 0; t < targets.size(); ++t)
                {
                    const Target& target = targets[t];
                    if (!fields[t] || pass > target.passes)
                        continue;
                    const auto start = std::chrono::steady_clock::now();
                    const surface_refine::Part& part = parts[target.part];
                    const surface_refine::Layout& layout = placed[t].layout;
                    workers.Step(item + " " + Describe(part.owner));
                    cache.Record("shade", [&]()
                    {
                        fronts[t].assign(layout.points.size(), MathColour());
                        backs[t].assign(layout.points.size(), MathColour());
                        workers.For(layout.points.size(), cooperate, [&](std::size_t i, std::size_t worker)
                        {
                            workers[worker].Watch(part.owner);
                            fronts[t][i] = shaders[worker]->BounceExport(layout.hits[i], layout.normals[i], layout.ids[i], part.directions);
                            if (part.inside)
                                backs[t][i] = shaders[worker]->BounceExport(layout.hits[i], -layout.normals[i], BounceOrderMix(layout.ids[i]),
                                                                            part.directions);
                        }, 0);
                    }, [&](CacheArchive& archive)
                    {
                        Serialize(archive, fronts[t]);
                        Serialize(archive, backs[t]);
                        if (archive.Reading() && ((fronts[t].size() != layout.points.size()) || (backs[t].size() != layout.points.size())))
                            throw CacheFormatError("a bounce pass's shading does not match its points");
                    });
                    if (td->preparationProgress)
                    {
                        char line[128];
                        std::snprintf(line, sizeof(line), "Bounce %s: %lu nodes shaded again, %.1fs", item.c_str(),
                                      static_cast<unsigned long>(layout.points.size()),
                                      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
                        td->preparationProgress->Note(line);
                    }
                }
                ReportHeaviest(workers, item);
            }
            for (std::size_t t = 0; t < targets.size(); ++t)
            {
                const Target& target = targets[t];
                if (!target.surface || placed[t].points == 0 || pass > target.passes)
                    continue;
                const auto field = surface_refine::Field(placed[t], pass > 1 ? fronts[t] : placed[t].layout.front,
                                                         pass > 1 ? backs[t] : placed[t].layout.back);
                const MediaLight& settings = *BounceSettings(*target.light);
                // a unit rescales the light's whole field at each pass, so what later passes take from it is its stated amount
                const double scale = PassScale(settings, field->IntegratedRadiance(), areas[t]);
                if (!(scale > 0.0) && (settings.lumens > 0.0 || (settings.nits && settings.brightness > 0.0)) && td->preparationProgress)
                    td->preparationProgress->Warn("Surface bounce on the " + Describe(target.light->emitterContainer) +
                                                  " received no light at pass " + std::to_string(pass) +
                                                  ", so its brightness in lumens or nits scales nothing.");
                fields[t] = std::make_shared<const SumSurfaceBounceRadiance>(
                    std::vector<std::shared_ptr<const SurfaceBounceRadiance>>{field}, scale);
                if (td->preparationProgress)
                {
                    char line[160];
                    std::snprintf(line, sizeof(line), "Bounce %s field: power %.9g before scaling, scaled by %.9g on the ", item.c_str(),
                                  M_PI * field->IntegratedRadiance().Greyscale(), scale);
                    td->preparationProgress->Note(line + Describe(target.light->emitterContainer), true);
                }
            }
            const unsigned int order = pass;
            const std::string orderItem = Of("o", index, maximum);
            std::vector<std::shared_ptr<LightSource>> next;
            std::vector<const LightSource*> nextOriginal;
            const bool nextVolume = std::any_of(targets.begin(), targets.end(),
                [&](const Target& target) { return !target.surface && pass < target.passes; });
            for (std::size_t t = 0; t < targets.size(); ++t)
                if (nextVolume && targets[t].surface && fields[t] && pass <= targets[t].passes)
                {
                    // Volume orders receive only this pass's change in the accumulated surface field.
                    std::shared_ptr<const SurfaceBounceRadiance> change = fields[t];
                    if (priorFields[t])
                        change = std::make_shared<const SumSurfaceBounceRadiance>(
                            std::vector<std::shared_ptr<const SurfaceBounceRadiance>>{fields[t],
                                std::make_shared<const TintedSurfaceBounceRadiance>(priorFields[t], MathColour(-1.0))});
                    auto light = Clone(targets[t].light);
                    light->cutoff = 0.0;
                    light->emitter = MakeBouncedSurfaceEmitter(targets[t].primary,
                        const_cast<ObjectPtr>(targets[t].light->emitterContainer), change, false);
                    light->colour = light->emitter->Intensity() / M_PI;
                    next.push_back(std::move(light));
                    nextOriginal.push_back(targets[t].light);
                }
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
            for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
            {
                Target& target = targets[targetIndex];
                if (order > target.passes || target.surface)
                    continue;
                const auto owner = target.light->emitterContainer;
                const Sources sources = VolumeSources(*scene, target, order, previous, previousOriginal);
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
                    cache.Record("moments", [&]()
                    {
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
                            workers[worker].Watch(target.light->emitterContainer);
                            TraceTicket ticket(256, 0.0);
                            thread_local std::vector<IncidentDraw> draws;
                            thread_local std::vector<int> scratch;
                            (*moments)[node] = GatherVolumeIncident(workers[worker].trace, at, cellSize, gather,
                                                                    BounceOrderSeed(0, order, groupIndex, node), ticket, draws, scratch);
                        });
                    }, [](CacheArchive&) {});
                    cache.Grid("moment nodes", *moments, target.domain->NodeCounts(),
                               LatticeBounds(target.domain->Low(), target.domain->CellSize(), true));
                    AdvanceLoaded(cache, td, target.domain->NodeCount());
                    for (size_t phase = 0; phase < prepared.phases.size(); ++phase)
                        prepared.fields.push_back(std::make_shared<const BounceMomentGrid>(
                            target.domain, moments, prepared.phases[phase].type, prepared.phases[phase].eccentricity, phase == 0));
                    std::vector<const Target*> contributing;
                    for (const auto& candidate : targets)
                        if (!candidate.surface && candidate.domain == target.domain &&
                            order <= candidate.passes &&
                            BounceGatherDraws(candidate.light->emitterContainer) == draws &&
                            VolumeSources(*scene, candidate, order, previous, previousOriginal) == sources)
                            contributing.push_back(&candidate);
                    std::vector<DBL> proposalMasses(target.domain->CellCount());
                    workers.Step(step + " proposal");
                    cache.Record("proposal", [&]()
                    {
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
                    }, [](CacheArchive&) {});
                    cache.Grid("proposal cells", proposalMasses, target.domain->Counts(),
                               LatticeBounds(target.domain->Low(), target.domain->CellSize(), false));
                    AdvanceLoaded(cache, td, target.domain->CellCount());
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
                if (surfacePasses > 0)
                    light->cutoff = 0.0;
                light->selfLight = nullptr;
                light->selfContainer = nullptr;
                light->selfScale = MathColour(1.0);
                light->emitter = MakeBouncedInteriorEmitter(*target.primary, field, td, false, target.proposal, &cache);
                light->colour = light->emitter->Intensity() / M_PI;
                next.push_back(std::move(light));
                nextOriginal.push_back(target.light);
            }
            ReportHeaviest(workers, "o" + std::to_string(order));
            previous.swap(next);
            previousOriginal.swap(nextOriginal);
        }
        for (std::size_t t = 0; t < targets.size(); ++t)
            if (fields[t])
            {
                Feed(*targets[t].light, targets[t].primary, const_cast<ObjectPtr>(targets[t].light->emitterContainer), fields[t],
                     targets[t].self);
                targets[t].light->bounceFed = false;
            }
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
            cache.Record("final proposal", [&]()
            {
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
            }, [](CacheArchive&) {});
            cache.Grid("final proposal cells", masses, target.domain->Counts(),
                       LatticeBounds(target.domain->Low(), target.domain->CellSize(), false));
            AdvanceLoaded(cache, td, target.domain->CellCount());
            finalProposals.push_back(
                {target.domain, std::make_shared<const PreparedSourceDistribution>(target.domain, masses)});
        }
    for (Target& target : targets)
    {
        if (target.surface)
            continue;
        target.proposal = std::find_if(finalProposals.begin(), finalProposals.end(),
            [&](const FinalProposal& candidate) { return candidate.domain == target.domain; })->proposal;
        FeedVolume(target, td, cache);
        target.light->bounceFed = false;
    }
    cache.Finish();
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
