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

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/lighting/lightgroup.h"
#include "core/lighting/lightsource.h"
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
PhotonSortingTask::PhotonSortingTask(ViewData *vd, const std::vector<PhotonShootingTask*>& shootingTasks,
                                     PhotonShootingStrategy* strategy,
                                     size_t seed) :
    RenderTask(vd, seed, "Photon"),
    shootingTasks(shootingTasks),
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
        for (PhotonShootingTask* task : shootingTasks)
        {
            if (PhotonMap* map = task->getSurfacePhotonMap(id))
                set.surfacePhotonMap.mergeMap(map);
            if (PhotonMap* map = task->getMediaPhotonMap(id))
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

}
// end of namespace pov
