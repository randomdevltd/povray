//******************************************************************************
///
/// @file backend/lighting/photonsortingtask.cpp
///
/// This module implements Photon Mapping.
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
#include "backend/lighting/photonsortingtask.h"

// C++ variants of C standard header files
// C++ standard header files
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <iomanip>
#include <sstream>
#include <unordered_map>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/lighting/lightgroup.h"
#include "core/lighting/lightsource.h"
#include "core/lighting/progressivephotonmap.h"
#include "core/lighting/progressivevolumemap.h"
#include "core/material/interior.h"
#include "core/material/media.h"
#include "core/math/matrix.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"
#include "core/support/octree.h"

// POV-Ray header files (POVMS module)
#include "povms/povmscpp.h"
#include "povms/povmsid.h"
#include "povms/povmsutil.h"

// POV-Ray header files (backend module)
#include "backend/control/messagefactory.h"
#include "backend/lighting/photonshootingstrategy.h"
#include "backend/lighting/photonshootingtask.h"
#include "backend/scene/backendscenedata.h"
#include "backend/scene/view.h"
#include "backend/scene/viewthreaddata.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

namespace
{
bool LegacyPhotonMapEligible(const SceneData& scene)
{
    if (scene.preparedSets.size() != 1)
        return false;
    for (const auto& alias : scene.preparedSetFilters)
        if (alias.first.specified)
            return false;
    return true;
}

std::string PhotonSettingsKey(const ScenePhotonSettings& settings)
{
    std::ostringstream key;
    key << std::setprecision(std::numeric_limits<DBL>::max_digits10)
        << "count=" << settings.surfaceCount << ';';
    if (settings.surfaceCount == 0)
        key << "spacing=" << settings.surfaceSeparation << ';';
    key << "trace=" << settings.Max_Trace_Level
        << ";adc=" << settings.adcBailout
        << ";jitter=" << settings.jitter
        << ";autostop=" << settings.autoStopPercent
        << ";mediaSpacing=" << settings.mediaSpacingFactor
        << ";mediaSteps=" << settings.maxMediaSteps;
    return key.str();
}
}

/*
    If you pass a nullptr for the "strategy" parameter, then this will
    load the photon map from a file.
    Otherwise, it will:
      1) merge
      2) sort
      3) compute gather options
      4) clean up memory (delete the non-merged maps and delete the strategy)
*/
PhotonSortingTask::PhotonSortingTask(ViewData *vd, const std::vector<ViewThreadData*>& shooters,
                                     PhotonShootingStrategy* strategy,
                                     size_t seed) :
    RenderTask(vd, seed, "Photon"),
    shooters(shooters),
    strategy(strategy),
    cooperate(*this)
{
}

PhotonSortingTask::~PhotonSortingTask()
{
}

void PhotonSortingTask::SendProgress(void)
{
#if 0
    // TODO FIXME PHOTONS
    // for now, we won't send this, as it can be confusing on the front-end due to out-of-order delivery from multiple threads.
    // we need to create a new progress message for sorting.
    if (timer.ElapsedRealTime() > 1000)
    {
        timer.Reset();
        POVMS_Object obj(kPOVObjectClass_PhotonProgress);
        obj.SetInt(kPOVAttrib_CurrentPhotonCount, (GetSceneData()->surfacePhotonMap.numPhotons + GetSceneData()->mediaPhotonMap.numPhotons));
        RenderBackend::SendViewOutput(GetViewData()->GetViewId(), GetSceneData()->frontendAddress, kPOVMsgIdent_Progress, obj);
    }
#endif
}

void PhotonSortingTask::Run()
{
    // quit right away if photons not enabled
    if (!GetSceneData()->photonSettings.photonsEnabled) return;

    Cooperate();

    if (strategy != nullptr)
    {
        strategy->finishShooting();
        delete strategy;
        sortPhotonMap();
    }
    else
    {
        if (!this->load())
            mpMessageFactory->Error(POV_EXCEPTION_STRING("Failed to load photon map from disk"), "Could not load photon map (%s)",GetSceneData()->photonSettings.fileName.c_str());

        // set photon options automatically
        for (const auto& set : GetSceneData()->preparedSets)
        {
            if (set->surfacePhotonMap.numPhotons > 0)
                set->surfacePhotonMap.setGatherOptions(GetSceneData()->photonSettings, false);
            if (set->mediaPhotonMap.numPhotons > 0)
                set->mediaPhotonMap.setGatherOptions(GetSceneData()->photonSettings, true);
        }
    }

    // good idea to make sure all warnings and errors arrive frontend now [trf]
    SendProgress();
    Cooperate();
}

void PhotonSortingTask::Stopped()
{
    // nothing to do for now [trf]
}

void PhotonSortingTask::Finish()
{
    GetViewDataPtr()->timeType = TraceThreadData::kPhotonTime;
    GetViewDataPtr()->realTime = ConsumedRealTime();
    GetViewDataPtr()->cpuTime = ConsumedCPUTime();
}


void PhotonSortingTask::sortPhotonMap()
{
    for (PreparedSetId id = 0; id < GetSceneData()->preparedSets.size(); ++id)
    {
        PreparedSet& set = GetSceneData()->GetPreparedSet(id);
        set.surfacePhotonMap.truncate(0);
        set.mediaPhotonMap.truncate(0);
        for (ViewThreadData* shooter : shooters)
        {
            if (PhotonMap* map = shooter->FindSurfacePhotonMap(id))
                set.surfacePhotonMap.mergeMap(map);
            if (PhotonMap* map = shooter->FindMediaPhotonMap(id))
                set.mediaPhotonMap.mergeMap(map);
        }
        if (set.surfacePhotonMap.numPhotons > 0)
        {
            set.surfacePhotonMap.buildTree();
            set.surfacePhotonMap.setGatherOptions(GetSceneData()->photonSettings, false);
        }
        if (set.mediaPhotonMap.numPhotons > 0)
        {
            set.mediaPhotonMap.buildTree();
            set.mediaPhotonMap.setGatherOptions(GetSceneData()->photonSettings, true);
        }
    }

#ifdef GLOBAL_PHOTONS
    /* ----------- global photons ------------- */
    if (globalPhotonMap.numPhotons>0)
    {
        globalPhotonMap.buildTree();
        globalPhotonMap.setGatherOptions(false);
    }
#endif

    int totalPhotons = 0;
    for (const auto& set : GetSceneData()->preparedSets)
        totalPhotons += set->surfacePhotonMap.numPhotons + set->mediaPhotonMap.numPhotons;
    if (totalPhotons+
#ifdef GLOBAL_PHOTONS
        globalPhotonMap.numPhotons+
#endif
        0 > 0)
    {
        /* should we save the photon map now that it is built? */
        if (!GetSceneData()->photonSettings.fileName.empty() && !GetSceneData()->photonSettings.loadFile)
        {
            /* status bar for user */
//          Send_Progress("Saving Photon Maps", PROGRESS_SAVING_PHOTON_MAPS);
            if (!this->save())
                mpMessageFactory->Warning(kWarningGeneral,"Could not save photon map.");
        }
    }
    else
    {
        if (!GetSceneData()->photonSettings.fileName.empty() && !GetSceneData()->photonSettings.loadFile)
            mpMessageFactory->Warning(kWarningGeneral,"Could not save photon map - no photons!");
    }
}


/* savePhotonMap()

  Saves the caustic photon map to a file.

  Preconditions:
    InitBacktraceEverything was called
    the photon map has been built and balanced
    photonSettings.fileName contains the filename to save

  Postconditions:
    Returns true if success, false if failure.
    If success, the photon map has been written to the file.
*/
bool PhotonSortingTask::save()
{
    FILE *f = fopen(GetSceneData()->photonSettings.fileName.c_str(), "wb");
    if (!f)
        return false;
    auto writeValue = [f](const void *value, std::size_t size)
    {
        return fwrite(value, size, 1, f) == 1;
    };
    auto writeMap = [&](const PhotonMap& map)
    {
        const std::int32_t count = map.numPhotons;
        if (!writeValue(&count, sizeof(count)))
            return false;
        for (std::int32_t i = 0; i < count; ++i)
            if (!writeValue(&map.GetPhoton(i), sizeof(Photon)))
                return false;
        return true;
    };

    bool ok = true;
    if (LegacyPhotonMapEligible(*GetSceneData()))
    {
        const PreparedSet& set = GetSceneData()->GetPreparedSet(0);
        ok = writeMap(set.surfacePhotonMap) && writeMap(set.mediaPhotonMap);
    }
    else
    {
        static const unsigned char magic[8] = {'P','O','V','P','H','M','2',0};
        const std::uint32_t version = 1;
        const std::uint32_t setCount = GetSceneData()->preparedSets.size();
        ok = writeValue(magic, sizeof(magic)) && writeValue(&version, sizeof(version)) && writeValue(&setCount, sizeof(setCount));
        const std::string settingsKey = PhotonSettingsKey(GetSceneData()->photonSettings);
        const std::uint32_t settingsLength = settingsKey.size();
        ok = ok && writeValue(&settingsLength, sizeof(settingsLength));
        if (settingsLength > 0)
            ok = ok && writeValue(settingsKey.data(), settingsLength);
        std::vector<const PreparedSet*> sets;
        for (const auto& set : GetSceneData()->preparedSets)
            sets.push_back(set.get());
        std::sort(sets.begin(), sets.end(), [](const PreparedSet* a, const PreparedSet* b) { return a->photonKey < b->photonKey; });
        for (const PreparedSet* set : sets)
        {
            const std::uint32_t keyLength = set->photonKey.size();
            ok = ok && writeValue(&keyLength, sizeof(keyLength));
            if (keyLength > 0)
                ok = ok && writeValue(set->photonKey.data(), keyLength);
            ok = ok && writeMap(set->surfacePhotonMap) && writeMap(set->mediaPhotonMap);
        }
    }
    const bool closed = fclose(f) == 0;
    return ok && closed;
}

/* loadPhotonMap()

  Loads the caustic photon map from a file.

  Preconditions:
    InitBacktraceEverything was called
    the photon map is empty
    renderer->sceneData->photonSettings.fileName contains the filename to load

  Postconditions:
    Returns true if success, false if failure.
    If success, the photon map has been loaded from the file.
    If failure then the render should stop with an error
*/
bool PhotonSortingTask::load()
{
    if (!GetSceneData()->photonSettings.photonsEnabled) return false;

    mpMessageFactory->Warning(kWarningGeneral,"Starting the load of photon file %s\n",GetSceneData()->photonSettings.fileName.c_str());

    FILE *f = fopen(GetSceneData()->photonSettings.fileName.c_str(), "rb");
    if (!f)
        return false;
    auto readValue = [f](void *value, std::size_t size)
    {
        return fread(value, size, 1, f) == 1;
    };
    auto readMap = [&](PhotonMap& map)
    {
        std::int32_t count;
        if (!readValue(&count, sizeof(count)) || count < 0)
            return false;
        const long position = ftell(f);
        if (position < 0 || fseek(f, 0, SEEK_END) != 0)
            return false;
        const long end = ftell(f);
        if (end < position || fseek(f, position, SEEK_SET) != 0 ||
            std::uint64_t(count) > std::uint64_t(end - position) / sizeof(Photon))
            return false;
        for (std::int32_t i = 0; i < count; ++i)
            if (!readValue(map.AllocatePhoton(), sizeof(Photon)))
                return false;
        return true;
    };

    static const unsigned char magic[8] = {'P','O','V','P','H','M','2',0};
    unsigned char prefix[8];
    bool ok = readValue(prefix, sizeof(prefix));
    if (ok && std::memcmp(prefix, magic, sizeof(magic)) == 0)
    {
        std::uint32_t version, setCount, settingsLength;
        ok = readValue(&version, sizeof(version)) && version == 1 && readValue(&setCount, sizeof(setCount));
        ok = ok && readValue(&settingsLength, sizeof(settingsLength)) && settingsLength <= 1024 * 1024;
        std::string settingsKey(settingsLength, '\0');
        if (ok && settingsLength > 0)
            ok = readValue(&settingsKey[0], settingsLength);
        ok = ok && settingsKey == PhotonSettingsKey(GetSceneData()->photonSettings);
        std::map<std::string, PreparedSet*> expected;
        for (const auto& set : GetSceneData()->preparedSets)
            ok = ok && expected.emplace(set->photonKey, set.get()).second;
        std::map<std::string, std::pair<std::unique_ptr<PhotonMap>, std::unique_ptr<PhotonMap>>> loaded;
        for (std::uint32_t i = 0; ok && i < setCount; ++i)
        {
            std::uint32_t keyLength;
            ok = readValue(&keyLength, sizeof(keyLength)) && keyLength <= 1024 * 1024;
            std::string key(keyLength, '\0');
            if (ok && keyLength > 0)
                ok = readValue(&key[0], keyLength);
            auto maps = std::make_pair(std::unique_ptr<PhotonMap>(new PhotonMap()), std::unique_ptr<PhotonMap>(new PhotonMap()));
            ok = ok && readMap(*maps.first) && readMap(*maps.second) && loaded.emplace(key, std::move(maps)).second;
        }
        ok = ok && loaded.size() == expected.size();
        for (const auto& item : expected)
            ok = ok && loaded.find(item.first) != loaded.end();
        if (ok)
            for (const auto& item : expected)
            {
                auto found = loaded.find(item.first);
                item.second->surfacePhotonMap.truncate(0);
                item.second->mediaPhotonMap.truncate(0);
                item.second->surfacePhotonMap.mergeMap(found->second.first.get());
                item.second->mediaPhotonMap.mergeMap(found->second.second.get());
            }
    }
    else
    {
        const bool eligible = LegacyPhotonMapEligible(*GetSceneData());
        if (!eligible)
            mpMessageFactory->PossibleError("A legacy photon map cannot be loaded into filtered or multiple prepared sets; regenerate the map.");
        ok = ok && eligible && fseek(f, 0, SEEK_SET) == 0;
        PhotonMap surface, media;
        ok = ok && readMap(surface);
        if (ok)
        {
            const int next = fgetc(f);
            if (next != EOF)
            {
                ungetc(next, f);
                ok = readMap(media);
            }
        }
        if (ok)
        {
            PreparedSet& set = GetSceneData()->GetPreparedSet(0);
            set.surfacePhotonMap.truncate(0);
            set.mediaPhotonMap.truncate(0);
            set.surfacePhotonMap.mergeMap(&surface);
            set.mediaPhotonMap.mergeMap(&media);
        }
    }
    const bool closed = fclose(f) == 0;
    return ok && closed;
}


namespace
{
const unsigned char kProgressiveMagic[8] = {'P','O','V','P','H','M','Q',0}, kProgressiveEnd[8] = {'P','H','M','Q','E','N','D',0};
const std::uint32_t kProgressiveVersion = 1, kAtmosphere = 0xFFFFFFFFu;

/// Every object, interior and light a photon can name, numbered in a traversal fixed by the scene's structure.
struct ProgressiveSceneIndex final
{
    std::vector<ConstObjectPtr> objects;
    std::unordered_map<ConstObjectPtr, std::uint32_t> objectIds;
    std::vector<const Interior*> interiors;
    std::unordered_map<const Interior*, std::uint32_t> interiorIds;
    std::unordered_map<const Media*, std::pair<std::uint32_t, std::uint32_t>> mediaIds;
    std::unordered_map<const LightSource*, std::uint32_t> lightIds;
    const SceneData& scene;

    explicit ProgressiveSceneIndex(const SceneData& scene) : scene(scene)
    {
        for (ObjectPtr object : scene.objects)
            Visit(object);
        for (const auto* lights : {&scene.lightSources, &scene.lightGroupLightSources})
            for (size_t i = 0; i < lights->size(); ++i)
            {
                Visit((*lights)[i]);
                lightIds.emplace((*lights)[i], std::uint32_t(2 * i + (lights == &scene.lightSources ? 1 : 2)));
            }
        for (const auto& set : scene.preparedSets)
            for (ObjectPtr view : set->views)
                Visit(view);
        for (size_t i = 0; i < scene.atmosphere.size(); ++i)
            mediaIds.emplace(&scene.atmosphere[i], std::make_pair(kAtmosphere, std::uint32_t(i)));
    }

    void Visit(ConstObjectPtr object)
    {
        if (object == nullptr || !objectIds.emplace(object, std::uint32_t(objects.size())).second)
            return;
        objects.push_back(object);
        if (const Interior* interior = object->interior.get())
            if (interiorIds.emplace(interior, std::uint32_t(interiors.size())).second)
            {
                for (size_t i = 0; i < interior->media.size(); ++i)
                    mediaIds.emplace(&interior->media[i], std::make_pair(std::uint32_t(interiors.size()), std::uint32_t(i)));
                interiors.push_back(interior);
            }
        if (const auto* compound = dynamic_cast<const CompoundObject*>(object))
            for (ObjectPtr child : compound->children)
                Visit(child);
    }

    const LightSource* Light(std::uint32_t code) const
    {
        const auto& lights = (code & 1) ? scene.lightSources : scene.lightGroupLightSources;
        const size_t index = (code - 1) / 2;
        return code == 0 ? nullptr : index < lights.size() ? lights[index] : nullptr;
    }

    std::string Shape() const
    {
        std::ostringstream shape;
        shape << objects.size() << " objects, " << interiors.size() << " interiors and "
              << scene.lightSources.size() + scene.lightGroupLightSources.size() << " lights";
        return shape.str();
    }
};

class PhotonFileWriter final
{
    public:
        explicit PhotonFileWriter(const std::string& name) : file(std::fopen(name.c_str(), "wb")) {}
        ~PhotonFileWriter() { if (file) std::fclose(file); }
        template<class T> void Put(const T& value) { Bytes(&value, sizeof(value)); }
        void Bytes(const void* data, size_t size) { ok = ok && file && (size == 0 || std::fwrite(data, size, 1, file) == 1); }
        void String(const std::string& text) { Put(std::uint32_t(text.size())); Bytes(text.data(), text.size()); }
        void Colour(const MathColour& colour) { for (int c = 0; c < MathColour::channels; ++c) Put(colour[c]); }
        bool Close() { const bool closed = file && std::fclose(file) == 0; file = nullptr; return ok && closed; }
    private:
        std::FILE* file;
        bool ok = true;
};

class PhotonFileReader final
{
    public:
        explicit PhotonFileReader(const std::string& name) : file(std::fopen(name.c_str(), "rb"))
        {
            if (file && std::fseek(file, 0, SEEK_END) == 0)
            {
                const long end = std::ftell(file);
                remaining = end > 0 ? std::uint64_t(end) : 0;
                ok = std::fseek(file, 0, SEEK_SET) == 0;
            }
        }
        ~PhotonFileReader() { if (file) std::fclose(file); }
        bool Opened() const { return file != nullptr; }
        bool Good() const { return ok; }
        std::uint64_t Remaining() const { return remaining; }
        template<class T> T Get() { T value{}; Bytes(&value, sizeof(value)); return value; }
        void Bytes(void* data, size_t size)
        {
            ok = ok && size <= remaining && (size == 0 || std::fread(data, size, 1, file) == 1);
            remaining = ok ? remaining - size : 0;
        }
        std::string String()
        {
            const std::uint32_t size = Get<std::uint32_t>();
            std::string text(Fits(size, 1) ? size : 0, '\0');
            Bytes(&text[0], text.size());
            return text;
        }
        MathColour Colour() { MathColour colour; for (int c = 0; c < MathColour::channels; ++c) colour[c] = Get<ColourChannel>(); return colour; }
        /// Whether count records of the given size can still follow; a damaged count fails here instead of allocating.
        bool Fits(std::uint64_t count, std::uint64_t size) { ok = ok && count <= remaining / size; return ok; }
    private:
        std::FILE* file;
        std::uint64_t remaining = 0;
        bool ok = false;
};

constexpr std::uint64_t kSurfaceRecord = 3 * 8 + 6 * 4 + 4, kSegmentRecord = 7 * 8 + 4 + 4;
}

std::string ProgressivePhotonIdentity(const SceneData& scene)
{
    const ScenePhotonSettings& settings = scene.photonSettings;
    const ProgressivePhotonBudget budget(settings.quality);
    std::ostringstream key;
    key << std::setprecision(std::numeric_limits<DBL>::max_digits10)
        << "quality=" << settings.quality << ";batches=" << budget.batches << ";batch=" << ProgressivePhotonBudget::batchSize
        << ";shards=" << ProgressivePhotonBudget::shards << ";k=" << budget.neighbours << ";sobol=" << PhotonSobol::kDimensions
        << ";trace=" << settings.Max_Trace_Level << ";adc=" << settings.adcBailout
        << ";mediaSteps=" << settings.maxMediaSteps << ";mediaSpacing=" << settings.mediaSpacingFactor
        << ";channels=" << MathColour::channels << 'x' << sizeof(ColourChannel) << ";targets=";
    const ProgressiveSceneIndex index(scene);
    std::vector<const PhotonTargetGroup*> groups;
    for (size_t i = 0; i < index.objects.size(); ++i)
        if (Test_Flag(index.objects[i], PH_TARGET_FLAG))
        {
            key << i << ':' << index.objects[i]->Ph_Density;
            if (const PhotonTargetGroup* group = index.objects[i]->photonTargetGroup.get())
            {
                if (std::find(groups.begin(), groups.end(), group) == groups.end())
                    groups.push_back(group);
                key << 'g' << std::find(groups.begin(), groups.end(), group) - groups.begin();
            }
            key << ',';
        }
    return key.str();
}

std::string SaveProgressivePhotons(const SceneData& scene, std::uint64_t seed)
{
    const ProgressiveSceneIndex index(scene);
    PhotonFileWriter out(scene.photonSettings.fileName);
    out.Bytes(kProgressiveMagic, sizeof(kProgressiveMagic));
    out.Put(kProgressiveVersion);
    out.String(ProgressivePhotonIdentity(scene));
    out.Put(seed);
    out.String(index.Shape());
    out.Put(std::uint32_t(scene.preparedSets.size()));
    for (const auto& set : scene.preparedSets)
    {
        out.String(set->photonKey);
        const auto& surface = set->progressiveSurface;
        out.Put(std::uint32_t(surface.Receivers().size()));
        for (ConstObjectPtr receiver : surface.Receivers())
        {
            const auto found = index.objectIds.find(receiver);
            if (found == index.objectIds.end())
                return "a photon receiver is not part of the scene's object tree";
            out.Put(found->second);
        }
        out.Put(std::uint64_t(surface.photons.size()));
        for (const MappedPhoton& photon : surface.photons)
        {
            for (unsigned int a = 0; a < 3; ++a)
                out.Put(photon.point[a]);
            out.Bytes(photon.direction, sizeof(photon.direction));
            out.Bytes(photon.normal, sizeof(photon.normal));
            out.Colour(photon.flux);
            out.Put(std::uint32_t(photon.receiver));
        }
        std::unordered_map<const ProgressiveVolumeDomain*, std::uint32_t> domainIds;
        std::vector<const ProgressiveVolumeDomain*> domains;
        for (const auto& segment : set->progressiveMedia.photons)
            if (domainIds.emplace(segment.domain.get(), std::uint32_t(domains.size())).second)
                domains.push_back(segment.domain.get());
        out.Put(std::uint32_t(domains.size()));
        for (const ProgressiveVolumeDomain* domain : domains)
        {
            out.Put(std::uint32_t(domain->media.size()));
            for (const Media* medium : domain->media)
            {
                const auto found = index.mediaIds.find(medium);
                if (found == index.mediaIds.end())
                    return "a media photon's medium is not part of the scene";
                out.Put(found->second.first);
                out.Put(found->second.second);
            }
            out.Put(std::uint32_t(domain->modifiers.size()));
            for (size_t i = 0; i < domain->modifiers.size(); ++i)
            {
                const auto found = index.interiorIds.find(domain->modifiers[i]);
                if (found == index.interiorIds.end())
                    return "a media photon's modifier is not part of the scene";
                out.Put(found->second);
                out.Put(std::uint64_t(domain->below[i]));
            }
        }
        out.Put(std::uint64_t(set->progressiveMedia.photons.size()));
        for (const auto& segment : set->progressiveMedia.photons)
        {
            for (unsigned int a = 0; a < 3; ++a)
                out.Put(segment.point[a]);
            for (unsigned int a = 0; a < 3; ++a)
                out.Put(segment.direction[a]);
            out.Colour(segment.flux);
            out.Colour(segment.extinction);
            out.Put(segment.length);
            out.Put(domainIds.at(segment.domain.get()));
            const auto light = index.lightIds.find(segment.light);
            if (segment.light != nullptr && light == index.lightIds.end())
                return "a media photon's light is not part of the scene";
            out.Put(light == index.lightIds.end() ? std::uint32_t(0) : light->second);
        }
    }
    out.Bytes(kProgressiveEnd, sizeof(kProgressiveEnd));
    return out.Close() ? std::string() : "the file could not be written";
}

std::string LoadProgressivePhotons(SceneData& scene, double budget, std::uint64_t& savedSeed)
{
    const std::string& name = scene.photonSettings.fileName;
    PhotonFileReader in(name);
    if (!in.Opened())
        return "Cannot open the photon file '" + name + "'.";
    const std::string damaged = "The photon file '" + name + "' is truncated or damaged.";
    unsigned char magic[sizeof(kProgressiveMagic)];
    in.Bytes(magic, sizeof(magic));
    if (!in.Good() || std::memcmp(magic, kProgressiveMagic, sizeof(magic)) != 0)
        return "'" + name + "' is not a photon method 2 file; classic photon maps cannot be loaded by method 2.";
    const std::uint32_t version = in.Get<std::uint32_t>();
    if (in.Good() && version != kProgressiveVersion)
        return "The photon file '" + name + "' has format version " + std::to_string(version) + "; this build reads version " +
               std::to_string(kProgressiveVersion) + ".";
    const std::string identity = in.String(), expected = ProgressivePhotonIdentity(scene);
    savedSeed = in.Get<std::uint64_t>();
    const ProgressiveSceneIndex index(scene);
    const std::string shape = in.String();
    if (in.Good() && shape != index.Shape())
        return "The photon file '" + name + "' was saved for a scene with " + shape + "; this scene has " + index.Shape() + ".";
    if (in.Good() && identity != expected)
        return "The photon file '" + name + "' was saved with other photon settings (" + identity + "); this scene has " + expected + ".";
    const std::uint32_t sets = in.Get<std::uint32_t>();
    std::map<std::string, PreparedSet*> wanted;
    for (const auto& set : scene.preparedSets)
        wanted.emplace(set->photonKey, set.get());
    if (in.Good() && (sets != scene.preparedSets.size() || wanted.size() != sets))
        return "The photon file '" + name + "' was saved for other prepared scene sets; regenerate it.";
    constexpr double segmentBytes = sizeof(ProgressiveVolumePhoton) + sizeof(ProgressiveVolumePhotonMap::Bounds);
    double bytes = 0.0;
    std::vector<const PreparedSet*> loaded;
    for (std::uint32_t s = 0; in.Good() && s < sets; ++s)
    {
        const auto target = wanted.find(in.String());
        if (in.Good() && target == wanted.end())
            return "The photon file '" + name + "' was saved for other prepared scene sets; regenerate it.";
        PreparedSet* set = in.Good() ? target->second : nullptr;
        if (set != nullptr && std::find(loaded.begin(), loaded.end(), set) != loaded.end())
            return damaged;
        loaded.push_back(set);
        const std::uint32_t receiverCount = in.Get<std::uint32_t>();
        std::vector<ConstObjectPtr> receivers;
        for (std::uint32_t i = 0; in.Fits(receiverCount, 4) && i < receiverCount; ++i)
        {
            const std::uint32_t id = in.Get<std::uint32_t>();
            if (id >= index.objects.size())
                return damaged;
            receivers.push_back(index.objects[id]);
        }
        const std::uint64_t count = in.Get<std::uint64_t>();
        if (!in.Fits(count, kSurfaceRecord + MathColour::channels * sizeof(ColourChannel)))
            return damaged;
        bytes += double(count) * sizeof(MappedPhoton);
        if (bytes > budget)
            break;
        std::vector<MappedPhoton> photons(count);
        for (MappedPhoton& photon : photons)
        {
            for (unsigned int a = 0; a < 3; ++a)
                photon.point[a] = in.Get<double>();
            in.Bytes(photon.direction, sizeof(photon.direction));
            in.Bytes(photon.normal, sizeof(photon.normal));
            photon.flux = in.Colour();
            const std::uint32_t receiver = in.Get<std::uint32_t>();
            if (receiver >= receivers.size())
                return damaged;
            photon.receiver = receiver;
            photon.axis = 0;
        }
        const std::uint32_t domainCount = in.Get<std::uint32_t>();
        std::vector<std::shared_ptr<const ProgressiveVolumeDomain>> domains;
        for (std::uint32_t d = 0; in.Fits(domainCount, 8) && d < domainCount; ++d)
        {
            auto domain = std::make_shared<ProgressiveVolumeDomain>();
            const std::uint32_t media = in.Get<std::uint32_t>();
            for (std::uint32_t m = 0; in.Fits(media, 8) && m < media; ++m)
            {
                const std::uint32_t owner = in.Get<std::uint32_t>(), slot = in.Get<std::uint32_t>();
                const std::vector<Media>* list = owner == kAtmosphere ? &scene.atmosphere :
                                                 owner < index.interiors.size() ? &index.interiors[owner]->media : nullptr;
                if (list == nullptr || slot >= list->size())
                    return damaged;
                domain->media.push_back(&(*list)[slot]);
            }
            const std::uint32_t modifiers = in.Get<std::uint32_t>();
            for (std::uint32_t m = 0; in.Fits(modifiers, 12) && m < modifiers; ++m)
            {
                const std::uint32_t owner = in.Get<std::uint32_t>();
                if (owner >= index.interiors.size())
                    return damaged;
                domain->modifiers.push_back(index.interiors[owner]);
                domain->below.push_back(size_t(in.Get<std::uint64_t>()));
            }
            domains.push_back(std::move(domain));
        }
        const std::uint64_t segments = in.Get<std::uint64_t>();
        if (!in.Fits(segments, kSegmentRecord + 2 * MathColour::channels * sizeof(ColourChannel)))
            return damaged;
        bytes += double(segments) * segmentBytes;
        if (bytes > budget)
            break;
        std::vector<ProgressiveVolumePhoton> media(segments);
        for (ProgressiveVolumePhoton& segment : media)
        {
            for (unsigned int a = 0; a < 3; ++a)
                segment.point[a] = in.Get<double>();
            for (unsigned int a = 0; a < 3; ++a)
                segment.direction[a] = in.Get<double>();
            segment.flux = in.Colour();
            segment.extinction = in.Colour();
            segment.length = in.Get<double>();
            const std::uint32_t domain = in.Get<std::uint32_t>(), light = in.Get<std::uint32_t>();
            if (domain >= domains.size() || (light != 0 && index.Light(light) == nullptr))
                return damaged;
            segment.domain = domains[domain];
            segment.light = index.Light(light);
            segment.grouped = segment.light != nullptr && segment.light->lightGroupLight;
            segment.set = std::uint32_t(std::find_if(scene.preparedSets.begin(), scene.preparedSets.end(),
                [set](const std::unique_ptr<PreparedSet>& candidate) { return candidate.get() == set; }) - scene.preparedSets.begin());
        }
        if (!in.Good())
            return damaged;
        set->progressiveSurface.Restore(std::move(receivers), std::move(photons));
        set->progressiveMedia.photons = std::move(media);
    }
    if (bytes > budget)
    {
        std::ostringstream message;
        message << std::setprecision(3) << "The photon file '" << name << "' needs at least " << bytes / 1.0e9
                << " GB for photons, more than the " << budget / 1.0e9 << " GB allowed (40% of available memory, at most 6 GB).";
        return message.str();
    }
    unsigned char end[sizeof(kProgressiveEnd)];
    in.Bytes(end, sizeof(end));
    if (!in.Good() || std::memcmp(end, kProgressiveEnd, sizeof(end)) != 0 || in.Remaining() != 0)
        return damaged;
    return std::string();
}

}
// end of namespace pov
