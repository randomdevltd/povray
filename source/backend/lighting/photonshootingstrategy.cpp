//******************************************************************************
///
/// @file backend/lighting/photonshootingstrategy.cpp
///
/// @todo   What's in here?
///
/// Author: Nathan Kopp
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
#include "backend/lighting/photonshootingstrategy.h"

// C++ variants of C standard header files
// C++ standard header files
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/lighting/lightgroup.h"
#include "core/lighting/lightsource.h"
#include "core/lighting/photons.h"
#include "core/material/normal.h"
#include "core/material/pigment.h"
#include "core/material/texture.h"
#include "core/math/matrix.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"
#include "core/support/octree.h"
#include "core/support/statistics.h"

#include "backend/scene/viewthreaddata.h"

// POV-Ray header files (POVMS module)
// POV-Ray header files (backend module)
//  (none at the moment)

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

static constexpr std::size_t kMaxSplitRings = std::size_t(1) << 26;  // total across pairs

template <typename Visit>
static bool walkRings(const LightTargetCombo& combo, std::size_t budget, Visit visit)
{
    DBL next = combo.mintheta;
    for (std::size_t index = 0; next < combo.maxtheta; ++index)
    {
        if (index >= budget)
            return false;
        visit(index, next);
        const DBL after = next + combo.dtheta;
        if (after <= next)
            return false;
        next = after;
    }
    return true;
}

void PhotonShootingStrategy::start()
{
    const std::size_t comboCount = units.size();
    const std::size_t workerCount = std::size_t(std::max(threads, 1));
    if (comboCount == 0 || comboCount >= workerCount)
    {
        iter = units.begin();
        return;
    }

    comboProgress.resize(comboCount);
    const std::size_t maxRingsPerCombo = kMaxSplitRings / comboCount;
    std::vector<std::size_t> ringCount(comboCount, 0);
    std::vector<std::size_t> firstPastStop(comboCount, 0);
    std::vector<std::size_t> chunkCount(comboCount, 1);
    for (std::size_t i = 0; i < comboCount; ++i)
    {
        const LightTargetCombo& combo = units[i]->lightAndObject;
        const bool concentricRings = combo.light->Parallel ||
            (combo.light->Area_Light && combo.light->Photon_Area_Light) ||
            (!combo.light->Area_Light && ((combo.light->Light_Type == POINT_SOURCE) ||
                                          (combo.light->Light_Type == SPOT_SOURCE)));
        if (!concentricRings || !std::isfinite(combo.dtheta) || combo.dtheta <= 0 ||
            !std::isfinite(combo.maxtheta))
            continue;
        const DBL stopTheta = autoStopPercent * combo.maxtheta;
        std::size_t count = 0;
        std::size_t pastStop = std::numeric_limits<std::size_t>::max();
        const bool complete = walkRings(combo, maxRingsPerCombo, [&](std::size_t index, DBL theta)
        {
            count = index + 1;
            if (pastStop == std::numeric_limits<std::size_t>::max() && theta > stopTheta)
                pastStop = index;
        });
        if (!complete || count < 2)
            continue;
        ringCount[i] = count;
        firstPastStop[i] = std::min(pastStop, count);
    }

    std::size_t extraChunks = 4 * (workerCount - comboCount);
    const std::size_t eligible = std::count_if(ringCount.begin(), ringCount.end(),
        [](std::size_t count) { return count != 0; });
    if (extraChunks >= eligible)
    {
        for (std::size_t i = 0; i < comboCount; ++i)
            if (ringCount[i] != 0)
                ++chunkCount[i];
        extraChunks -= eligible;
    }
    while (extraChunks > 0)
    {
        std::size_t best = comboCount;
        std::size_t bestRingsPerChunk = 0;
        for (std::size_t i = 0; i < comboCount; ++i)
        {
            if (chunkCount[i] >= ringCount[i])
                continue;
            const std::size_t ringsPerChunk = (ringCount[i] + chunkCount[i] - 1) / chunkCount[i];
            if (ringsPerChunk > bestRingsPerChunk)
            {
                best = i;
                bestRingsPerChunk = ringsPerChunk;
            }
        }
        if (best == comboCount)
            break;
        ++chunkCount[best];
        --extraChunks;
    }

    std::vector<std::vector<PhotonShootingUnit*>> dividedByCombo(comboCount);
    std::size_t dividedCount = 0;
    for (std::size_t comboIndex = 0; comboIndex < comboCount; ++comboIndex)
    {
        PhotonShootingUnit* whole = units[comboIndex];
        const LightTargetCombo& combo = whole->lightAndObject;
        if (chunkCount[comboIndex] > 1)
        {
            const std::size_t rings = ringCount[comboIndex];
            auto state = std::make_unique<ComboProgress>();
            state->ringCount = rings;
            state->firstPastStop = firstPastStop[comboIndex];
            state->ringStates.reset(new std::atomic<unsigned char>[rings]);
            for (std::size_t i = 0; i < rings; ++i)
                state->ringStates[i].store(0, std::memory_order_relaxed);
            comboProgress[combo.serial] = std::move(state);
            const std::size_t chunks = chunkCount[comboIndex];
            std::vector<DBL> chunkTheta(chunks);
            std::size_t nextChunk = 0;
            walkRings(combo, rings, [&](std::size_t index, DBL theta)
            {
                if (nextChunk < chunks && index == nextChunk * rings / chunks)
                    chunkTheta[nextChunk++] = theta;
            });
            std::vector<PhotonShootingUnit*>& comboUnits = dividedByCombo[comboIndex];
            comboUnits.reserve(chunks);
            for (std::size_t i = 0; i < chunks; ++i)
            {
                auto* unit = new PhotonShootingUnit(combo.light, combo.target);
                unit->lightAndObject = combo;
                unit->lightAndObject.mintheta = chunkTheta[i];
                unit->lightAndObject.maxtheta = i + 1 < chunks ? chunkTheta[i + 1] : combo.maxtheta;
                unit->lightAndObject.thetaIndexBase = i * rings / chunks;
                unit->lightAndObject.parallelChunk = true;
                comboUnits.push_back(unit);
            }
            delete whole;
            dividedCount += comboUnits.size();
            continue;
        }
        dividedByCombo[comboIndex].push_back(whole);
        ++dividedCount;
    }
    std::vector<PhotonShootingUnit*> divided;
    divided.reserve(dividedCount);
    for (std::size_t chunk = 0; divided.size() < dividedCount; ++chunk)
        for (const auto& comboUnits : dividedByCombo)
            if (chunk < comboUnits.size())
                divided.push_back(comboUnits[chunk]);
    units = std::move(divided);
    progress.resize(units.size());
    for (std::size_t i = 0; i < units.size(); ++i)
        units[i]->recordIndex = i;
    iter = units.begin();
}

void PhotonShootingStrategy::beginUnit(PhotonShootingUnit& unit, ViewThreadData* worker)
{
    if (!unit.lightAndObject.parallelChunk)
        return;
    UnitProgress& record = progress[unit.recordIndex];
    record.worker = worker;
}

void PhotonShootingStrategy::beginRing(PhotonShootingUnit& unit, ViewThreadData* worker)
{
    if (!unit.lightAndObject.parallelChunk || autoStopPercent >= 1)
        return;
    UnitProgress& record = progress[unit.recordIndex];
    record.rings.push_back({worker->surfacePhotonMap->numPhotons, 0,
        worker->mediaPhotonMap->numPhotons, 0, worker->Stats()[Number_Of_Photons_Shot], 0});
}

void PhotonShootingStrategy::recordRing(PhotonShootingUnit& unit, ViewThreadData* worker)
{
    if (!unit.lightAndObject.parallelChunk || autoStopPercent >= 1)
        return;
    UnitProgress& record = progress[unit.recordIndex];
    RingProgress& ring = record.rings.back();
    ring.surfaceEnd = worker->surfacePhotonMap->numPhotons;
    ring.mediaEnd = worker->mediaPhotonMap->numPhotons;
    ring.shotsEnd = worker->Stats()[Number_Of_Photons_Shot];
    const std::size_t ringIndex = unit.lightAndObject.thetaIndexBase + record.rings.size() - 1;
    ComboProgress& state = *comboProgress[unit.lightAndObject.serial];
    POV_ASSERT(ringIndex < state.ringCount);
    state.ringStates[ringIndex].store(worker->hitObject ? 1 : 2, std::memory_order_release);
    auto recordEarliest = [](std::atomic<std::uint64_t>& earliest, std::uint64_t index)
    {
        std::uint64_t previous = earliest.load(std::memory_order_relaxed);
        while (index < previous && !earliest.compare_exchange_weak(previous, index,
            std::memory_order_release, std::memory_order_relaxed)) {}
    };
    if (worker->hitObject)
        recordEarliest(state.earliestHitIndex, ringIndex);
    else if (ringIndex >= state.firstPastStop)
        recordEarliest(state.earliestEmptyIndex, ringIndex);
    std::lock_guard<std::mutex> lock(state.resultMutex);
    while (state.completedPrefix < state.ringCount &&
           state.cutoffIndex.load(std::memory_order_relaxed) == std::numeric_limits<std::uint64_t>::max())
    {
        const unsigned char ringState = state.ringStates[state.completedPrefix].load(std::memory_order_acquire);
        if (ringState == 0)
            break;
        state.prefixHasHit |= ringState == 1;
        if (state.prefixHasHit && ringState == 2 && state.completedPrefix >= state.firstPastStop)
            state.cutoffIndex.store(state.completedPrefix, std::memory_order_release);
        ++state.completedPrefix;
    }
}

bool PhotonShootingStrategy::pastCutoff(std::uint64_t comboSerial, std::uint64_t ringIndex) const
{
    if (comboSerial >= comboProgress.size() || !comboProgress[comboSerial])
        return false;
    const ComboProgress& state = *comboProgress[comboSerial];
    const std::uint64_t hit = state.earliestHitIndex.load(std::memory_order_acquire);
    const std::uint64_t empty = state.earliestEmptyIndex.load(std::memory_order_acquire);
    const std::uint64_t cutoff = state.cutoffIndex.load(std::memory_order_acquire);
    return ringIndex > (hit < empty ? std::min(cutoff, empty) : cutoff);
}

void PhotonShootingStrategy::finishShooting()
{
    if (autoStopPercent >= 1 || progress.empty())
        return;
    struct Removal
    {
        PhotonMap* map;
        int first;
        int last;
    };
    std::vector<Removal> surfaceRemovals;
    std::vector<Removal> mediaRemovals;
    for (std::size_t unitIndex = 0; unitIndex < progress.size(); ++unitIndex)
    {
        const PhotonShootingUnit& unit = *units[unitIndex];
        if (!unit.lightAndObject.parallelChunk)
            continue;
        const std::uint64_t cutoff = comboProgress[unit.lightAndObject.serial]->cutoffIndex.load(std::memory_order_acquire);
        if (cutoff == std::numeric_limits<std::uint64_t>::max())
            continue;
        UnitProgress& record = progress[unitIndex];
        if (record.worker == nullptr)
            continue;
        for (std::size_t r = 0; r < record.rings.size(); ++r)
        {
            const RingProgress& ring = record.rings[r];
            const std::uint64_t index = unit.lightAndObject.thetaIndexBase + r;
            if (index <= cutoff)
                continue;
            if (ring.surfaceEnd > ring.surfaceStart)
                surfaceRemovals.push_back({record.worker->surfacePhotonMap, ring.surfaceStart, ring.surfaceEnd});
            if (ring.mediaEnd > ring.mediaStart)
                mediaRemovals.push_back({record.worker->mediaPhotonMap, ring.mediaStart, ring.mediaEnd});
            record.worker->Stats()[Number_Of_Photons_Shot] -= ring.shotsEnd - ring.shotsStart;
            record.worker->Stats()[Number_Of_Photons_Stored] -= ring.surfaceEnd - ring.surfaceStart;
            record.worker->Stats()[Number_Of_Media_Photons_Stored] -= ring.mediaEnd - ring.mediaStart;
        }
    }
    auto eraseDescending = [](std::vector<Removal>& removals)
    {
        std::sort(removals.begin(), removals.end(), [](const Removal& a, const Removal& b)
        {
            return a.map == b.map ? a.first < b.first : std::less<PhotonMap*>()(a.map, b.map);
        });
        for (std::size_t i = 0; i < removals.size();)
        {
            const std::size_t first = i;
            std::vector<std::pair<int, int>> ranges;
            while (i < removals.size() && removals[i].map == removals[first].map)
            {
                const Removal& removal = removals[i++];
                ranges.emplace_back(removal.first, removal.last);
            }
            removals[first].map->eraseRanges(std::move(ranges));
        }
    };
    eraseDescending(surfaceRemovals);
    eraseDescending(mediaRemovals);
}

PhotonShootingUnit* PhotonShootingStrategy::getNextUnit()
{
    std::lock_guard<std::mutex> lock(nextUnitMutex);
    if (iter == units.end())
        return nullptr;
    PhotonShootingUnit* unit = *iter;
    iter++;
    return unit;
}

void PhotonShootingStrategy::createUnitsForCombo(ObjectPtr obj, LightSource* light, std::shared_ptr<SceneData> sceneData)
{
    PhotonShootingUnit* unit = new PhotonShootingUnit(light, obj);
    unit->lightAndObject.computeAnglesAndDeltas(sceneData);
    unit->lightAndObject.serial = units.size();
    units.push_back(unit);
}

PhotonShootingStrategy::~PhotonShootingStrategy()
{
    std::vector<PhotonShootingUnit*>::iterator delIter;
    for(delIter = units.begin(); delIter != units.end(); delIter++)
    {
        delete (*delIter);
    }
    units.clear();
}

}
// end of namespace pov
