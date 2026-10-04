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

void PhotonShootingStrategy::start()
{
    if (units.size() == 1 && threads > 1)
    {
        PhotonShootingUnit* whole = units.front();
        const LightTargetCombo& combo = whole->lightAndObject;
        if (combo.light->Parallel && std::isfinite(combo.dtheta) && combo.dtheta > 0 &&
            std::isfinite(combo.maxtheta))
        {
            std::vector<DBL> theta;
            DBL next = combo.mintheta;
            while (next < combo.maxtheta && theta.size() < 1000000)
            {
                theta.push_back(next);
                const DBL after = next + combo.dtheta;
                if (after <= next)
                    break;
                next = after;
            }
            if (next >= combo.maxtheta && theta.size() > 1)
            {
                fullMaxTheta = combo.maxtheta;
                const std::size_t chunks = std::min(theta.size(), std::size_t(threads) * 4);
                std::vector<PhotonShootingUnit*> divided;
                divided.reserve(chunks);
                for (std::size_t i = 0; i < chunks; ++i)
                {
                    const std::size_t begin = i * theta.size() / chunks;
                    const std::size_t end = (i + 1) * theta.size() / chunks;
                    auto* unit = new PhotonShootingUnit(combo.light, combo.target);
                    unit->lightAndObject = combo;
                    unit->lightAndObject.mintheta = theta[begin];
                    unit->lightAndObject.maxtheta = end < theta.size() ? theta[end] : fullMaxTheta;
                    unit->lightAndObject.thetaIndexBase = begin;
                    unit->lightAndObject.parallelChunk = true;
                    divided.push_back(unit);
                }
                delete whole;
                units = std::move(divided);
                split = true;
            }
        }
    }
    for (std::size_t i = 0; i < units.size(); ++i)
        units[i]->recordIndex = i;
    if (split)
        progress.resize(units.size());
    iter = units.begin();
}

void PhotonShootingStrategy::beginUnit(PhotonShootingUnit& unit, ViewThreadData* worker)
{
    if (!split)
        return;
    UnitProgress& record = progress[unit.recordIndex];
    record.worker = worker;
    record.surfaceStart = worker->surfacePhotonMap->numPhotons;
    record.mediaStart = worker->mediaPhotonMap->numPhotons;
    record.shotsStart = worker->Stats()[Number_Of_Photons_Shot];
}

void PhotonShootingStrategy::recordRing(PhotonShootingUnit& unit, ViewThreadData* worker, DBL theta)
{
    if (!split)
        return;
    progress[unit.recordIndex].rings.push_back({theta, worker->hitObject != 0,
        worker->surfacePhotonMap->numPhotons, worker->mediaPhotonMap->numPhotons,
        worker->Stats()[Number_Of_Photons_Shot]});
}

void PhotonShootingStrategy::finishShooting()
{
    if (!split)
        return;
    bool hit = false;
    std::size_t stopUnit = progress.size();
    std::size_t stopRing = 0;
    for (std::size_t u = 0; u < progress.size(); ++u)
    {
        for (std::size_t r = 0; r < progress[u].rings.size(); ++r)
        {
            const RingProgress& ring = progress[u].rings[r];
            hit |= ring.hit;
            if (hit && !ring.hit && ring.theta > autoStopPercent * fullMaxTheta)
            {
                stopUnit = u;
                stopRing = r;
                break;
            }
        }
        if (stopUnit != progress.size())
            break;
    }
    if (stopUnit == progress.size())
        return;
    // Each worker receives chunks in order, so discard later chunks first.
    for (std::size_t u = progress.size(); u-- > stopUnit; )
    {
        const UnitProgress& record = progress[u];
        if (record.worker == nullptr)
            continue;
        const bool last = u == stopUnit;
        const int surface = last ? record.rings[stopRing].surfaceEnd : record.surfaceStart;
        const int media = last ? record.rings[stopRing].mediaEnd : record.mediaStart;
        const std::uint64_t shots = last ? record.rings[stopRing].shotsEnd : record.shotsStart;
        record.worker->surfacePhotonMap->truncate(surface);
        record.worker->mediaPhotonMap->truncate(media);
        record.worker->Stats()[Number_Of_Photons_Shot] = shots;
        record.worker->Stats()[Number_Of_Photons_Stored] = surface;
        record.worker->Stats()[Number_Of_Media_Photons_Stored] = media;
    }
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
