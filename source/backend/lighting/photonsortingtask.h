//******************************************************************************
///
/// @file backend/lighting/photonsortingtask.h
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

#ifndef POVRAY_BACKEND_PHOTONSORTINGTASK_H
#define POVRAY_BACKEND_PHOTONSORTINGTASK_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "backend/configbackend.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdint>
#include <string>
#include <vector>

// POV-Ray header files (base module)
#include "core/lighting/photons_fwd.h"
#include "core/lighting/progressivephoton.h"

// POV-Ray header files (core module)
#include "core/render/trace.h"

// POV-Ray header files (backend module)
#include "backend/lighting/photonshootingstrategy_fwd.h"
#include "backend/render/rendertask.h"

namespace pov
{

class ViewThreadData;
class ScenePhotonSettings;
class SceneData;

/// The settings a method 2 photon file must have been shot with to be loaded.
std::string ProgressivePhotonIdentity(const SceneData& scene);
/// Writes every prepared set's merged, unbuilt method 2 maps to the photon file; returns why it failed, or nothing.
std::string SaveProgressivePhotons(const SceneData& scene, std::uint64_t seed);
/// The POV_PHOTON_MEMORY ceiling in bytes, read once: zero when unset, negative when it is not a positive number of GB.
double ProgressivePhotonEnvironmentCeiling();
/// The ceiling from the first source that sets it: the Photon_Memory option, POV_PHOTON_MEMORY, the scene's photons memory.
ProgressivePhotonCeiling ProgressivePhotonMemoryCeiling(const SceneData& scene);
/// The bytes method 2 photons may use: the ceiling, else 40% of the memory available now, at most 6 GB.
double ProgressivePhotonMemoryBudget(const ProgressivePhotonCeiling& ceiling);
/// Reads unbuilt maps written by SaveProgressivePhotons into the prepared sets; returns why it failed, or nothing.
std::string LoadProgressivePhotons(SceneData& scene, const ProgressivePhotonCeiling& ceiling, std::uint64_t& savedSeed);

class PhotonSortingTask final : public RenderTask
{
    public:
        Timer timer;

        /// the shooting tasks' thread data, which outlives the tasks themselves
        std::vector<ViewThreadData*> shooters;
        PhotonShootingStrategy* strategy;

        PhotonSortingTask(ViewData *vd, const std::vector<ViewThreadData*>& shooters,
                          PhotonShootingStrategy* strategy, size_t seed);
        virtual ~PhotonSortingTask() override;

        virtual void Run() override;
        virtual void Stopped() override;
        virtual void Finish() override;

        void SendProgress();

        void sortPhotonMap();
        bool save();
        bool load();
    private:
        class CooperateFunction final : public Trace::CooperateFunctor
        {
            public:
                CooperateFunction(Task& t) : task(t) { }
                virtual void operator()() override { task.Cooperate(); }
            private:
                Task& task;
        };

        CooperateFunction cooperate;
};

}
// end of namespace pov

#endif // POVRAY_BACKEND_PHOTONSORTINGTASK_H
