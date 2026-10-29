#ifndef POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H
#define POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H

#include "core/configcore.h"
#include "core/lighting/bouncegrid.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/photons.h"
#include "core/lighting/sharedincidentfield.h"
#include "core/lighting/subsurface.h"
#include "core/lighting/surfacebounce.h"
#include "core/material/interiorfield.h"
#include "core/material/media.h"
#include "core/material/pigment.h"
#include "core/material/preparedsourcefield.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/parallel.h"
#include "core/support/preparationprogress.h"
#include <string>
#include <algorithm>
#include <functional>
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
    const std::function<void()>& callback;
};

struct Context final
{
    Context(std::shared_ptr<SceneData> scene, TraceThreadData* td, const std::function<void()>& callback)
        : cooperate(callback), trace(scene, td, QualityFlags(9), cooperate, media, radiosity),
          media(td, &trace, &photons),
          photons(&scene->GetPreparedSet(0).surfacePhotonMap, scene->photonSettings)
    {
    }
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
        const auto advance = [count, units](std::size_t index) { return (index + 1) * units / count - index * units / count; };
        try
        {
            ParallelFor(count, contexts.size(), [&](std::size_t index, std::size_t worker)
            {
                stop();
                function(index, worker);
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
    std::shared_ptr<const PreparedSurfaceBounceResponse> surfaceResponse;
    std::vector<std::shared_ptr<const SurfaceBounceRadiance>> surfaceOrders;
};

// Progress units of a surface target's pass over its nodes, however many nodes its response finds.
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
                values[found - target.phases.begin()][node] += component.coefficient;
        }
    }, target.domain->NodeCount());
    for (auto& phase : values)
        if (constant)
            target.coefficients.push_back(std::make_shared<const PreparedConstantColourField>(phase.front()));
        else
            target.coefficients.push_back(
                std::make_shared<const PreparedSourceColourField>(target.domain, std::move(phase)));
}

// The subsurface light leaving surface bounce nodes; the workers share one cloud cache, whose cells depend only on their keys.
inline std::shared_ptr<const SurfaceBounceRadiance> PrepareSubsurfaceBounce(
    Workers& workers, const std::shared_ptr<SceneData>& scene, const std::vector<SurfaceBounceNode>& nodes,
    const std::vector<std::shared_ptr<LightSource>>& sources, const std::set<ObjectPtr>& collapsedOwners,
    const std::function<void()>& cooperate)
{
    if (!scene->useSubsurface)
        return nullptr;
    struct CacheScope
    {
        Workers& workers;
        std::vector<std::shared_ptr<SubsurfaceCache>> saved;
        ~CacheScope()
        {
            for (std::size_t worker = 0; worker < saved.size(); ++worker)
                workers.Thread(worker)->subsurfaceCache = saved[worker];
        }
    } scope{workers, {}};
    const auto cache = std::make_shared<SubsurfaceCache>(scene->subsurfaceMemory, scene->subsurfaceSpacing);
    std::vector<std::unique_ptr<Trace>> traces;
    for (std::size_t worker = 0; worker < workers.Count(); ++worker)
    {
        TraceThreadData* const thread = workers.Thread(worker);
        scope.saved.push_back(thread->subsurfaceCache);
        thread->subsurfaceCache = cache;
        Context& context = workers[worker];
        traces.emplace_back(new Trace(scene, thread, QualityFlags(9), context.cooperate, context.media, context.radiosity));
        traces.back()->BeginSurfaceSubsurface(sources);
    }
    std::vector<MathColour> radiance(nodes.size());
    std::vector<double> etas(nodes.size());
    workers.For(nodes.size(), cooperate, [&](size_t node, size_t worker)
    {
        radiance[node] = traces[worker]->SurfaceSubsurfaceNode(nodes[node], node, 0, etas[node]);
    }, kSurfaceUnits);
    traces.clear();
    return Trace::SurfaceSubsurfaceRadiance(nodes, radiance, etas, 4096, collapsedOwners);
}

// Bounce progress units, as the passes below count them; zero while a volume target's domain is unprepared.
inline std::size_t BounceUnits(const SceneData& scene)
{
    std::size_t units = 0;
    std::vector<std::shared_ptr<const PreparedSourceDomain>> domains;
    for (const LightSource* light : scene.mediaLights)
    {
        const MediaLight* const settings = BounceSettings(*light);
        if (!settings)
            continue;
        if (light->emitter->IsSurface())
        {
            units += 1 + settings->bouncePasses * (kSurfaceUnits * (scene.useSubsurface ? 2 : 1) + 1);
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
                     [](const LightSource* light) { return BounceSettings(*light) != nullptr; }))
        return;
    if (td->preparationProgress)
        td->preparationProgress->Begin("bounce", BounceUnits(*scene));
    Workers workers(scene, td);
    std::vector<ConstObjectPtr> objects;
    std::unordered_set<ConstObjectPtr> seen;
    for (const auto object : scene->objects)
        CollectObjects(object, objects, seen);
    std::vector<Target> targets;
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
        maximum = std::max(maximum, target.passes);
        if (surface)
        {
            target.surfaceResponse =
                PreparedSurfaceBounceResponse::Prepare(const_cast<ObjectPtr>(owner), td, kSurfaceUnits, cooperate);
            AdvancePreparation(td->preparationProgress);
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
    for (unsigned int index = 0; index < maximum; ++index)
    {
        const unsigned int order = index + 1;
        const std::string orderItem = Of("o", index, maximum);
        std::vector<std::shared_ptr<LightSource>> next;
        std::vector<const LightSource*> nextOriginal;
        struct SharedGroup
        {
            std::shared_ptr<const PreparedSourceDomain> domain;
            std::vector<LightSource*> sources;
            unsigned int samples;
            std::vector<PreparedBouncePhase> phases;
            std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> fields;
            std::shared_ptr<const PreparedSourceDistribution> proposal;
        };
        std::vector<SharedGroup> groups;
        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
        {
            Target& target = targets[targetIndex];
            if (order > target.passes)
                continue;
            const auto owner = target.light->emitterContainer;
            std::vector<LightSource*> sources;
            std::vector<std::shared_ptr<LightSource>> sharedSources;
            for (size_t source = 0; source < previous.size(); ++source)
                if (!previous[source]->Photon_Only && EmitsPreparedSource(*previous[source]) &&
                    (target.surfaceResponse || previous[source]->Media_Interaction) &&
                    Receives(owner, previousOriginal[source]))
                {
                    sources.push_back(previous[source].get());
                    sharedSources.push_back(previous[source]);
                }
            if (target.surfaceResponse)
            {
                const auto& nodes = target.surfaceResponse->Nodes();
                std::vector<std::vector<BounceIncidentBeam>> incident(nodes.size());
                const std::string step = orderItem + " " + Of("t", targetIndex, targets.size());
                workers.Step(step + " surface");
                workers.For(nodes.size(), cooperate, [&](size_t node, size_t worker)
                {
                    incident[node] =
                        CollectBounceIncident(workers[worker].trace, nodes[node].intersection.IPoint, sources,
                                              std::max(1, owner->surfaceLight->samples),
                                              BounceOrderSeed(0, order, targetIndex, node),
                                              nodes[node].intersection.Object, nodes[node].normal);
                }, kSurfaceUnits);
                if (scene->useSubsurface)
                    workers.Step(step + " subsurface");
                const auto diffuse = target.surfaceResponse->Scatter(incident);
                const auto subsurface = PrepareSubsurfaceBounce(workers, scene, nodes, sharedSources,
                                                                target.surfaceResponse->CollapsedChartOwners(), cooperate);
                const auto field = std::make_shared<const SumSurfaceBounceRadiance>(
                    std::vector<std::shared_ptr<const SurfaceBounceRadiance>>{diffuse, subsurface});
                AdvancePreparation(td->preparationProgress);
                target.surfaceOrders.push_back(field);
                auto light = Clone(target.light);
                light->emitter =
                    MakeBouncedSurfaceEmitter(target.primary, const_cast<ObjectPtr>(owner), field, false);
                light->colour = light->emitter->Intensity() / M_PI;
                next.push_back(std::move(light));
                nextOriginal.push_back(target.light);
                continue;
            }
            const unsigned int samples = std::max(1, owner->interior->light->samples);
            auto group = std::find_if(groups.begin(), groups.end(),
                                      [&](const SharedGroup& candidate)
                                      {
                                          return candidate.domain == target.domain &&
                                                 candidate.samples == samples && candidate.sources == sources;
                                      });
            if (group != groups.end())
                AdvancePreparation(td->preparationProgress, target.domain->NodeCount() + target.domain->CellCount());
            if (group == groups.end())
            {
                SharedGroup prepared{target.domain, sources, samples, {}, {}, nullptr};
                for (const auto& candidate : targets)
                    if (!candidate.surfaceResponse && candidate.domain == target.domain)
                        for (const auto& phase : candidate.phases)
                            if (std::find(prepared.phases.begin(), prepared.phases.end(), phase) ==
                                prepared.phases.end())
                                prepared.phases.push_back(phase);
                std::vector<std::vector<BounceFieldNode>> phaseNodes(prepared.phases.size());
                for (size_t phase = 0; phase < phaseNodes.size(); ++phase)
                {
                    phaseNodes[phase].reserve(target.domain->NodeCount());
                    for (size_t node = 0; node < target.domain->NodeCount(); ++node)
                        phaseNodes[phase].emplace_back(samples,
                                                       BounceOrderSeed(phase, order, groups.size(), node));
                }
                const size_t groupIndex = groups.size();
                const std::string step = orderItem + " " + Of("t", targetIndex, targets.size());
                workers.Step(step + " volume");
                workers.For(target.domain->NodeCount(), cooperate, [&](size_t node, size_t worker)
                {
                    const auto beams =
                        CollectBounceIncident(workers[worker].trace, target.domain->Node(node), sources, samples,
                                              BounceOrderSeed(0, order, groupIndex, node));
                    for (size_t phase = 0; phase < prepared.phases.size(); ++phase)
                        for (const auto& beam : beams)
                            phaseNodes[phase][node].AddScatteringBeam(
                                prepared.phases[phase].type, prepared.phases[phase].eccentricity,
                                MathColour(1.0), beam.direction, beam.angularWeight);
                });
                for (auto& nodes : phaseNodes)
                    prepared.fields.push_back(
                        std::make_shared<const BounceFieldGrid>(target.domain, std::move(nodes)));
                std::vector<const Target*> contributing;
                for (const auto& candidate : targets)
                    if (!candidate.surfaceResponse && candidate.domain == target.domain &&
                        order <= candidate.passes &&
                        unsigned(std::max(1, candidate.light->emitterContainer->interior->light->samples)) ==
                            samples)
                    {
                        std::vector<LightSource*> candidateSources;
                        for (size_t source = 0; source < previous.size(); ++source)
                            if (!previous[source]->Photon_Only && previous[source]->Media_Interaction &&
                                EmitsPreparedSource(*previous[source]) &&
                                Receives(candidate.light->emitterContainer, previousOriginal[source]))
                                candidateSources.push_back(previous[source].get());
                        if (candidateSources == sources)
                            contributing.push_back(&candidate);
                    }
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
        if (!target.surfaceResponse &&
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
                    if (!candidate.surfaceResponse && candidate.domain == target.domain &&
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
        if (target.surfaceResponse)
        {
            const auto field =
                std::make_shared<const SumSurfaceBounceRadiance>(std::move(target.surfaceOrders));
            target.light->emitter = MakeBouncedSurfaceEmitter(
                target.primary, const_cast<ObjectPtr>(target.light->emitterContainer), field, true);
        }
        else
        {
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
        if (target.surfaceResponse)
            continue;
        for (const auto& field : target.light->emitterContainer->interior->fields)
            if (field->owner == target.light->emitterContainer)
                for (auto& component : field->lights)
                    if (component.source == target.light)
                        component.payload = target.light->emitter->PreparedAxis();
    }
    if (td->preparationProgress)
        td->preparationProgress->End();
}

} // namespace pov

#endif
