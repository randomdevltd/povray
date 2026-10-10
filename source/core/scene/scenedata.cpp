//******************************************************************************
///
/// @file core/scene/scenedata.cpp
///
/// @todo   What's in here?
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/scene/scenedata.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <climits>
#include <sstream>

// POV-Ray header files (base module)
#include "base/types.h"
#include "base/version_info.h"
#include "base/image/colourspace.h"

// POV-Ray header files (core module)
#include "core/material/noise.h"
#include "core/material/pattern.h"
#include "core/bounding/boundingbox.h"
#include "core/bounding/bsptree.h"
#include "core/scene/atmosphere.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"
#include "core/shape/portal.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

PreparedSet::~PreparedSet()
{
    if (boundingSlabs != nullptr)
        Destroy_BBox_Tree(boundingSlabs);
    delete flatSlabs;
    delete tree;
    for (ObjectPtr view : views)
    {
        static_cast<CompoundObject *>(view)->children.clear();
        delete view;
    }
}

namespace
{

const std::vector<std::string>& EffectiveTags(ConstObjectPtr object, const std::vector<std::string> *inherited,
                                              std::vector<std::string>& merged)
{
    if ((inherited == nullptr) || inherited->empty() ||
        std::includes(object->tags.begin(), object->tags.end(), inherited->begin(), inherited->end()))
        return object->tags;
    merged = object->tags;
    MergeTags(merged, *inherited);
    return merged;
}

// An untagged compound only groups its children, so they decide; anything else is judged on its own tags.
bool Excluded(const CSG *csg, const std::vector<std::string>& tags, const TagFilter& filter)
{
    return ((csg == nullptr) || !tags.empty()) && !MatchesTags(tags, filter);
}

TagSelection ExcludedAs(ConstObjectPtr object)
{
    return Test_Flag(object, INVERTED_FLAG) ? TagSelection::Everything : TagSelection::Empty;
}

// Whether a child with this selection is dropped from its parent, and whether it collapses the parent instead.
bool Drops(TagSelection child, bool intersection)
{
    return (child == (intersection ? TagSelection::Everything : TagSelection::Empty));
}

bool Collapses(TagSelection child, bool intersection)
{
    return (child == (intersection ? TagSelection::Empty : TagSelection::Everything));
}

void CollectLights(ObjectPtr object, std::vector<LightSource*>& lights)
{
    if (LightSource *light = dynamic_cast<LightSource *>(object))
        lights.push_back(light);
    if (CompoundObject *compound = dynamic_cast<CompoundObject *>(object))
        for (ObjectPtr child : compound->children)
            CollectLights(child, lights);
}

void Rebound(CSG *csg)
{
    if (!csg->Bound.empty())
        return;
    Make_BBox(csg->BBox, -BOUND_HUGE/2, -BOUND_HUGE/2, -BOUND_HUGE/2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE);
    csg->Compute_BBox();
    Update_Infinite_Flag(csg);
}

TagSelection PruneTagged(ObjectPtr object, const TagFilter& filter, const std::vector<std::string> *inherited,
                         std::vector<LightSource*>& removedLights)
{
    std::vector<std::string> merged;
    const std::vector<std::string>& tags = EffectiveTags(object, inherited, merged);
    CSG *csg = dynamic_cast<CSG *>(object);
    if (Excluded(csg, tags, filter))
        return ExcludedAs(object);
    if (csg == nullptr)
        return TagSelection::Whole;
    const bool intersection = (dynamic_cast<CSGIntersection *>(csg) != nullptr);
    std::vector<TagSelection> selected;
    selected.reserve(csg->children.size());
    for (ObjectPtr child : csg->children)
    {
        selected.push_back(PruneTagged(child, filter, &tags, removedLights));
        if (Collapses(selected.back(), intersection))
            return selected.back();
    }
    std::vector<ObjectPtr> kept;
    bool part = false;
    for (size_t i = 0; i < csg->children.size(); ++i)
    {
        if (Drops(selected[i], intersection))
        {
            CollectLights(csg->children[i], removedLights);
            Destroy_Object(csg->children[i]);
            part = true;
        }
        else
        {
            kept.push_back(csg->children[i]);
            part = part || (selected[i] == TagSelection::Part);
        }
    }
    csg->children.swap(kept);
    if (csg->children.empty())
        return intersection ? TagSelection::Everything : TagSelection::Empty;
    if (part)
        Rebound(csg);
    return part ? TagSelection::Part : TagSelection::Whole;
}

CSG *NewView(CSG *original)
{
    CSG *view;
    if (CSGMerge *merge = dynamic_cast<CSGMerge *>(original))
        view = new CSGMerge(*merge, false);
    else if (CSGIntersection *intersection = dynamic_cast<CSGIntersection *>(original))
        view = new CSGIntersection(intersection->isDifference, *intersection, false);
    else
        view = new CSGUnion(original->Type, *original, false);
    view->Type = original->Type;
    view->Trans = Copy_Transform(original->Trans);
    view->do_split = original->do_split;
    view->children.clear();
    return view;
}

// A view of a compound holding only what the filter selects; `key` records the selection for set identity.
ObjectPtr ViewTagged(ObjectPtr object, const TagFilter& filter, const std::vector<std::string> *inherited,
                     std::vector<ObjectPtr>& views, std::ostringstream& key)
{
    std::vector<std::string> merged;
    const std::vector<std::string>& tags = EffectiveTags(object, inherited, merged);
    CSG *original = static_cast<CSG *>(object);
    const bool intersection = (dynamic_cast<CSGIntersection *>(original) != nullptr);
    CSG *view = NewView(original);
    key << '(';
    for (size_t i = 0; i < original->children.size(); ++i)
    {
        ObjectPtr child = original->children[i];
        const TagSelection selected = SelectTagged(child, filter, &tags);
        if (Drops(selected, intersection))
            continue;
        key << i;
        view->children.push_back((selected == TagSelection::Part) ? ViewTagged(child, filter, &tags, views, key) : child);
        key << ',';
    }
    key << ')';
    views.push_back(view);
    return view;
}

}

TagSelection SelectTagged(ConstObjectPtr object, const TagFilter& filter, const std::vector<std::string> *inherited)
{
    if (!filter.specified)
        return TagSelection::Whole;
    std::vector<std::string> merged;
    const std::vector<std::string>& tags = EffectiveTags(object, inherited, merged);
    const CSG *csg = dynamic_cast<const CSG *>(object);
    if (Excluded(csg, tags, filter))
        return ExcludedAs(object);
    if (csg == nullptr)
        return TagSelection::Whole;
    const bool intersection = (dynamic_cast<const CSGIntersection *>(csg) != nullptr);
    bool part = false, kept = false;
    for (ConstObjectPtr child : csg->children)
    {
        const TagSelection selected = SelectTagged(child, filter, &tags);
        if (Collapses(selected, intersection))
            return selected;
        part = part || (selected != TagSelection::Whole);
        kept = kept || !Drops(selected, intersection);
    }
    if (!kept)
        return intersection ? TagSelection::Everything : TagSelection::Empty;
    return part ? TagSelection::Part : TagSelection::Whole;
}

bool EnclosingTagsMatch(ConstObjectPtr object, const TagFilter& filter)
{
    for (const TagScope *scope = object->enclosingTags.get(); scope != nullptr; scope = scope->enclosing.get())
        if (!MatchesTags(scope->tags, filter))
            return false;
    return true;
}

TagSelection PruneTagged(ObjectPtr object, const TagFilter& filter, std::vector<LightSource*>& removedLights)
{
    if (!filter.specified)
        return TagSelection::Whole;
    return PruneTagged(object, filter, nullptr, removedLights);
}

void ForgetLights(ObjectPtr object, const std::vector<LightSource*>& lights)
{
    auto& own = object->LLights;
    own.erase(std::remove_if(own.begin(), own.end(), [&](LightSource *light) {
        return std::find(lights.begin(), lights.end(), light) != lights.end(); }), own.end());
    if (CompoundObject *compound = dynamic_cast<CompoundObject *>(object))
        for (ObjectPtr child : compound->children)
            ForgetLights(child, lights);
}

TagFilter SceneData::EffectiveFilterTags(const TagFilter& requested) const
{
    return requested.specified ? requested : defaultFilterTags;
}

namespace
{
void CollectParticipatingObjects(ObjectPtr object, std::unordered_set<ConstObjectPtr>& objects)
{
    if (!objects.insert(object).second)
        return;
    if (const CompoundObject *compound = dynamic_cast<const CompoundObject *>(object))
        for (ObjectPtr child : compound->children)
            CollectParticipatingObjects(child, objects);
}
}

PreparedSetId SceneData::RegisterPreparedSet(const TagFilter& filter)
{
    auto found = preparedSetFilters.find(filter);
    if (found != preparedSetFilters.end())
        return found->second;
    std::unique_ptr<PreparedSet> set(new PreparedSet());
    std::unordered_set<ConstObjectPtr> participating;
    std::ostringstream key;
    auto appendTags = [&](const std::vector<std::string>& tags) {
        key << ':' << tags.size() << ':';
        for (const std::string& tag : tags)
            key << tag.size() << ':' << tag;
        key << ';';
    };
    for (size_t ordinal = 0; ordinal < objects.size(); ++ordinal)
    {
        ObjectPtr object = objects[ordinal];
        if (!EnclosingTagsMatch(object, filter))
            continue;
        const TagSelection selected = SelectTagged(object, filter);
        if ((selected == TagSelection::Empty) || (selected == TagSelection::Everything))
            continue;
        key << 'o' << ordinal;
        appendTags(object->tags);
        if (selected == TagSelection::Part)
            object = ViewTagged(object, filter, nullptr, set->views, key);
        set->objects.push_back(object);
        CollectParticipatingObjects(object, participating);
    }
    // a media light takes part with the object holding its medium
    auto takesPart = [&](const LightSource *light) {
        return (participating.count(light) != 0) || ((light->emitterContainer != nullptr) && (participating.count(light->emitterContainer) != 0)); };
    for (const auto *sources : { &lightSources, &lightGroupLightSources })
        for (size_t ordinal = 0; ordinal < sources->size(); ++ordinal)
        {
            LightSource *light = (*sources)[ordinal];
            if (takesPart(light))
            {
                set->lights.push_back(light);
                key << (sources == &lightSources ? 'g' : 'l') << ordinal;
                appendTags(light->tags);
            }
        }
    for (size_t i = 0; i < preparedSets.size(); ++i)
        if (preparedSets[i]->photonKey == key.str())
        {
            preparedSetFilters.emplace(filter, i);
            return i;
        }
    set->surfacePhotonMap.minGatherRad = surfacePhotonMinGatherRad;
    set->surfacePhotonMap.minGatherRadMult = surfacePhotonMinGatherRadMult;
    set->mediaPhotonMap.minGatherRad = mediaPhotonMinGatherRad;
    set->mediaPhotonMap.minGatherRadMult = mediaPhotonMinGatherRadMult;
    set->photonKey = key.str();
    set->globalLightIn.assign(lightSources.size(), false);
    set->groupLightIn.assign(lightGroupLightSources.size(), false);
    set->groupPortalImages.resize(lightGroupLightSources.size());
    auto admitted = [&](const LightSource *image) { return participating.count(image->portal) != 0; };
    for (size_t i = 0; i < lightSources.size(); ++i)
        if (takesPart(lightSources[i]))
        {
            set->globalLights.push_back(i);
            set->globalLightIn[i] = true;
        }
    for (const LightSource *light : lightSources)
        if (takesPart(light))
            for (const LightSource *image : light->portalImages)
                if (admitted(image))
                    set->globalPortalImages.push_back(image);
    for (size_t i = 0; i < lightGroupLightSources.size(); ++i)
    {
        set->groupLightIn[i] = takesPart(lightGroupLightSources[i]);
        set->groupLightsFiltered = set->groupLightsFiltered || !set->groupLightIn[i];
        if (set->groupLightIn[i])
            for (const LightSource *image : lightGroupLightSources[i]->portalImages)
                if (admitted(image))
                    set->groupPortalImages[i].push_back(image);
    }
    for (const Portal *mouth : portalMouths)
        if (participating.count(mouth) != 0)
            set->portalMouths.push_back(mouth);
    const PreparedSetId id = preparedSets.size();
    preparedSets.push_back(std::move(set));
    preparedSetFilters.emplace(filter, id);
    return id;
}

PreparedSetId SceneData::FindPreparedSet(const TagFilter& filter) const
{
    return preparedSetFilters.at(filter);
}

bool SceneData::LightInPreparedSet(const LightSource& light, PreparedSetId index) const
{
    const LightSource& real = (light.imageOf != nullptr) ? *light.imageOf : light;
    const std::vector<bool>& in = real.lightGroupLight ? GetPreparedSet(index).groupLightIn : GetPreparedSet(index).globalLightIn;
    return (real.index < in.size()) && in[real.index];
}

void SceneData::FinalizeScreenCameras()
{
    screenCameras.clear();
    std::unordered_set<const Camera*> seen;
    for (const std::weak_ptr<const Camera>& candidate : screenCameraCandidates)
        if (std::shared_ptr<const Camera> camera = candidate.lock())
            if (seen.insert(camera.get()).second)
                screenCameras.push_back(std::move(camera));
    screenCameraCandidates.clear();
}

SceneData::SceneData() :
    fog(nullptr),
    rainbow(nullptr),
    skysphere(nullptr),
    functionContextFactory()
{
    atmosphereIOR = 1.0;
    atmosphereDispersion = 0.0;
    backgroundColour = ToTransColour(RGBFTColour(0.0, 0.0, 0.0, 0.0, 1.0));
    ambientLight = MathColour(1.0);

    iridWavelengths = MathColour::DefaultWavelengths();

    languageVersion = POV_RAY_VERSION_INT;
    languageVersionSet = false;
    languageVersionLate = false;
    pov4Version = 400;
    warningLevel = 10; // all warnings
    legacyCharset = LegacyCharset::kUnspecified;
    noiseGenerator = kNoiseGen_RangeCorrected;
    explicitNoiseGenerator = false; // scene has not set the noise generator explicitly
    boundingMethod = 0;
    numberOfWaves = 10;
    parsedMaxTraceLevel = MAX_TRACE_LEVEL_DEFAULT;
    parsedAdcBailout = 1.0 / 255.0; // adc bailout sufficient for displays
    workingGamma.reset();
    workingGammaToSRGB.reset();
    inputFileGamma = SRGBGammaCurve::Get();
    gammaMode = kPOVList_GammaMode_None; // default setting for v3.6.2, which in turn is the default for the language

    mmPerUnit = 10;
    useSubsurface = false;
    subsurfaceSamplesDiffuse = 50;
    subsurfaceSamplesSingle = 50;
    subsurfaceUseRadiosity = false;
    subsurfaceMethod = kSubsurfaceMethodSampled;
    explicitSubsurfaceMethod = false;
    mediaBlendModes = false;
    solidBlocksInteriorMedia = true;
    interiorMedia = false;
    mediaRefraction = false;
    legacyIorStack = true;
    refractionAngle = 0.5;
    dielectrics = false;
    anyOpaque = false;
    firstMediaPrecedence = UINT_MAX;
    subsurfaceErrorBound = 0.1;
    subsurfaceSpacing = 1.0;

    bspMaxDepth = 0;
    bspObjectIsectCost = bspBaseAccessCost = bspChildAccessCost = bspMissChance = 0.0f;

    Fractal_Iteration_Stack_Length = 0;
    Max_Blob_Components = 1000; // TODO FIXME - this gets set in the parser but allocated *before* that in the scene data, and if it is 0 here, a malloc may fail there because the memory requested is zero [trf]
    Max_Bounding_Cylinders = 100; // TODO FIXME - see note for Max_Blob_Components
    boundingSlabs = nullptr;
    flatSlabs = nullptr;

    splitUnions = false;
    removeBounds = true;

    tree = nullptr;
}

SceneData::~SceneData()
{
    preparedSets.clear();
    lightSources.clear();
    lightGroupLightSources.clear();
    for (LightSource *light : mediaLights)
        Destroy_Object(light);
    mediaLights.clear();
    for (LightSource *image : portalLights)
        Destroy_Object(image);
    portalLights.clear();
    Destroy_Skysphere(skysphere);
    while (fog != nullptr)
    {
        FOG *next = fog->Next;
        Destroy_Fog(fog);
        fog = next;
    }
    while (rainbow != nullptr)
    {
        RAINBOW *next = rainbow->Next;
        Destroy_Rainbow(rainbow);
        rainbow = next;
    }
    if (boundingSlabs != nullptr)
        Destroy_BBox_Tree(boundingSlabs);
    delete flatSlabs;
    for (std::vector<TrueTypeFont*>::iterator i = TTFonts.begin(); i != TTFonts.end(); ++i)
        delete *i;
    // TODO: perhaps ObjectBase::~ObjectBase would be a better place
    //       to handle cleanup of individual objects ?
    Destroy_Object(objects);

    if (tree != nullptr)
        delete tree;
}

}
// end of namespace pov
