#ifndef POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H
#define POVRAY_CORE_LIGHTING_BOUNCEPREPARE_H

#include "core/configcore.h"
#include "core/lighting/bouncegrid.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/photons.h"
#include "core/lighting/sharedincidentfield.h"
#include "core/lighting/surfacebounce.h"
#include "core/material/interiorfield.h"
#include "core/material/media.h"
#include "core/material/pigment.h"
#include "core/material/preparedsourcefield.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
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

inline bool Receives(ConstObjectPtr owner, const LightSource* light)
{
    if (!light->lightGroupLight)
        return !Test_Flag(owner, NO_GLOBAL_LIGHTS_FLAG);
    return std::find(owner->LLights.begin(), owner->LLights.end(), light) != owner->LLights.end();
}

inline void CollectObjects(ConstObjectPtr object, std::vector<ConstObjectPtr>& objects)
{
    if (!object || std::find(objects.begin(), objects.end(), object) != objects.end())
        return;
    objects.push_back(object);
    if (const auto compound = dynamic_cast<const CompoundObject*>(object))
        for (const auto child : compound->children)
            CollectObjects(child, objects);
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

inline void PrepareCoefficients(Target& target, Context& context, TraceThreadData* td,
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
    for (size_t node = 0; node < (constant ? 1 : target.domain->NodeCount()); ++node)
    {
        if (cooperate)
            cooperate();
        const auto& point = target.domain->Node(node);
        RayInteriorVector interiors;
        if (constant)
            interiors.push_back(owner->interior.get());
        else
        {
            if (!Inside_Object(point, const_cast<ObjectPtr>(owner), td))
                continue;
            context.trace.FindContainingInteriors(point, interiors);
        }
        for (const auto& component : EvaluateOwnerBounceScattering(interiors, *owner->interior, point, td))
        {
            const auto found = std::find(target.phases.begin(), target.phases.end(),
                                         PreparedBouncePhase{component.type, component.eccentricity});
            if (found != target.phases.end())
                values[found - target.phases.begin()][node] += component.coefficient;
        }
    }
    for (auto& phase : values)
        if (constant)
            target.coefficients.push_back(std::make_shared<const PreparedConstantColourField>(phase.front()));
        else
            target.coefficients.push_back(
                std::make_shared<const PreparedSourceColourField>(target.domain, std::move(phase)));
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
    Context context(scene, td, cooperate);
    std::vector<ConstObjectPtr> objects;
    for (const auto object : scene->objects)
        CollectObjects(object, objects);
    std::vector<Target> targets;
    unsigned int maximum = 0;
    for (LightSource* light : scene->mediaLights)
    {
        const auto owner = light->emitterContainer;
        if (!owner || !light->emitter)
            continue;
        const bool surface = light->emitter->IsSurface();
        const auto settings =
            surface ? owner->surfaceLight : (owner->interior ? owner->interior->light : nullptr);
        if (!settings || settings->bouncePasses == 0)
            continue;
        Target target;
        target.light = light;
        target.primary = light->emitter;
        target.passes = settings->bouncePasses;
        maximum = std::max(maximum, target.passes);
        if (surface)
        {
            target.surfaceResponse =
                PreparedSurfaceBounceResponse::Prepare(const_cast<ObjectPtr>(owner), td, 4096, cooperate);
            if (td->mediaMessages)
                td->mediaMessages->Info(
                    "Prepared surface bounce: %u nodes; %u orders; %u samples per source.",
                    unsigned(target.surfaceResponse->Nodes().size()), target.passes,
                    unsigned(std::max(1, settings->samples)));
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
        PrepareCoefficients(target, context, td, objects, cooperate);
        maximum = std::max(maximum, target.passes);
        targets.push_back(std::move(target));
    }
    if (targets.empty())
        return;
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
                for (size_t node = 0; node < nodes.size(); ++node)
                {
                    if (cooperate)
                        cooperate();
                    incident[node] =
                        CollectBounceIncident(context.trace, nodes[node].intersection.IPoint, sources,
                                              std::max(1, owner->surfaceLight->samples),
                                              BounceOrderSeed(0, order, targetIndex, node),
                                              nodes[node].intersection.Object, nodes[node].normal);
                }
                const auto diffuse = target.surfaceResponse->Scatter(incident);
                const auto subsurface = context.trace.PrepareSurfaceSubsurfaceRadiance(
                    nodes, sharedSources, 0, 4096, target.surfaceResponse->CollapsedChartOwners());
                const auto field = std::make_shared<const SumSurfaceBounceRadiance>(
                    std::vector<std::shared_ptr<const SurfaceBounceRadiance>>{diffuse, subsurface});
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
                for (size_t node = 0; node < target.domain->NodeCount(); ++node)
                {
                    if (cooperate)
                        cooperate();
                    const auto beams =
                        CollectBounceIncident(context.trace, target.domain->Node(node), sources, samples,
                                              BounceOrderSeed(0, order, groups.size(), node));
                    for (size_t phase = 0; phase < prepared.phases.size(); ++phase)
                        for (const auto& beam : beams)
                            phaseNodes[phase][node].AddScatteringBeam(
                                prepared.phases[phase].type, prepared.phases[phase].eccentricity,
                                MathColour(1.0), beam.direction, beam.angularWeight);
                }
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
                for (size_t cell = 0; cell < proposalMasses.size(); ++cell)
                {
                    const auto point = target.domain->CellMidpoint(cell);
                    MathColour value;
                    for (const auto candidate : contributing)
                        if (Inside_Object(point, const_cast<ObjectPtr>(candidate->light->emitterContainer),
                                          td))
                            for (size_t phase = 0; phase < candidate->phases.size(); ++phase)
                            {
                                const size_t index = std::find(prepared.phases.begin(), prepared.phases.end(),
                                                               candidate->phases[phase]) -
                                                     prepared.phases.begin();
                                value += candidate->coefficients[phase]->Sample(point) *
                                         prepared.fields[index]->AngularAverage(point);
                            }
                    proposalMasses[cell] = value.Weight() * target.domain->CellVolume(cell);
                }
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
            for (size_t cell = 0; cell < masses.size(); ++cell)
            {
                const auto point = target.domain->CellMidpoint(cell);
                MathColour value;
                for (const auto& candidate : targets)
                    if (!candidate.surfaceResponse && candidate.domain == target.domain &&
                        Inside_Object(point, const_cast<ObjectPtr>(candidate.light->emitterContainer), td))
                    {
                        value += candidate.primary->PreparedAxis()->ColourAt(point);
                        for (const auto& order : candidate.orders)
                            value += order->AngularAverage(point);
                    }
                masses[cell] = value.Weight() * target.domain->CellVolume(cell);
            }
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
}

} // namespace pov

#endif
