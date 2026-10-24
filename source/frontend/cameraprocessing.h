//******************************************************************************
///
/// @file frontend/cameraprocessing.h
///
/// The views of a render that draws several cameras from one parse.
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

#ifndef POVRAY_FRONTEND_CAMERAPROCESSING_H
#define POVRAY_FRONTEND_CAMERAPROCESSING_H

#include "frontend/configfrontend.h"

#include <functional>
#include <vector>

#include "base/stringtypes.h"

#include "povms/povmscpp.h"

namespace pov_frontend
{

using namespace pov_base;

/// The views of a scene's cameras with output blocks: each in turn, at its own size and window, into its own file.
class CameraProcessing final
{
    public:
        /// Takes the outputs of the cameras the parse collected, `name` giving the file an output name writes; throws if
        /// two cameras write one file.
        void Resolve(const std::vector<POVMS_Object>& cameraOutputs, POVMS_Object& options,
                     const std::function<UCS2String(const UCS2String&)>& name);
        bool Resolved() const { return resolved; }

        bool MoreCameras() const { return position + 1 < outputs.size(); }
        void NextCamera() { ++position; }
        unsigned int Position() const { return position; }
        unsigned int Count() const { return outputs.size(); }
        unsigned int CameraIndex() const { return position; }
        /// The output file of each camera, once resolved with output to a file.
        const std::vector<UCS2String>& Files() const { return files; }

        /// The view options of the current camera: `base` with its index, size, window and output file.
        POVMS_Object ViewOptions(const POVMS_Object& base) const;

        /// The default output file of camera `index` of `count`: `output` with `-<index + 1>`, zero-padded, before its extension.
        static UCS2String OutputFor(const UCS2String& output, unsigned int index, unsigned int count);
    private:
        bool resolved = false;
        unsigned int position = 0;
        std::vector<POVMS_Object> outputs;
        std::vector<UCS2String> files;
};

}
// end of namespace pov_frontend

#endif // POVRAY_FRONTEND_CAMERAPROCESSING_H
