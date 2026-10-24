//******************************************************************************
///
/// @file frontend/cameraprocessing.cpp
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "frontend/cameraprocessing.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <set>
#include <string>

// POV-Ray header files (base module)
#include "base/pov_err.h"
#include "base/stringutilities.h"

// POV-Ray header files (POVMS module)
#include "povms/povmsid.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov_frontend
{

void CameraProcessing::Resolve(const std::vector<POVMS_Object>& cameraOutputs, POVMS_Object& options,
                               const std::function<UCS2String(const UCS2String&)>& name)
{
    outputs = cameraOutputs;
    resolved = true;
    if (!options.TryGetBool(kPOVAttrib_OutputToFile, true))
        return;
    // without an output file name, the cameras' names are made from the one the render would write
    const UCS2String output(options.Exist(kPOVAttrib_OutputFile) ? options.TryGetUCS2String(kPOVAttrib_OutputFile, "") : name(UCS2String()));
    const UCS2String folder(output.substr(0, output.find_last_of(u"/\\:") + 1));
    std::set<UCS2String> written;
    for (unsigned int index = 0; index < outputs.size(); ++index)
    {
        POVMS_Object camera(outputs[index]);
        UCS2String file = camera.TryGetUCS2String(kPOVAttrib_OutputFile, "");
        if (file.empty())
            file = OutputFor(output, index, outputs.size());
        else if (file.find_first_of(u"/\\:") == UCS2String::npos)
            file = folder + file;
        file = name(file);
        if (!written.insert(file).second)
            throw POV_EXCEPTION(kParamErr, "Two cameras would write their images to the same file ('" + UCS2toSysString(file) +
                                           "'); give the cameras different output { file } names.");
        files.push_back(file);
    }
}

UCS2String CameraProcessing::OutputFor(const UCS2String& output, unsigned int index, unsigned int count)
{
    std::string number(std::to_string(index + 1));
    number.insert(0, std::to_string(count).size() - number.size(), '0');
    const UCS2String::size_type name = output.find_last_of(u"/\\:");
    const UCS2String::size_type dot = output.find_last_of(u'.');
    const UCS2String::size_type at = ((dot == UCS2String::npos) || ((name != UCS2String::npos) && (dot < name))) ? output.size() : dot;
    return output.substr(0, at) + SysToUCS2String("-" + number) + output.substr(at);
}

POVMS_Object CameraProcessing::ViewOptions(const POVMS_Object& base) const
{
    POVMS_Object options(base);
    POVMS_Object output(outputs[CameraIndex()]);
    options.SetInt(kPOVAttrib_CameraIndex, POVMSInt(CameraIndex()));
    if (output.Exist(kPOVAttrib_Width))
    {
        const POVMSInt width = output.GetInt(kPOVAttrib_Width), height = output.GetInt(kPOVAttrib_Height);
        options.SetInt(kPOVAttrib_Width, width);
        options.SetInt(kPOVAttrib_Height, height);
        // a camera of its own size renders all of it unless it gives its own window
        options.SetFloat(kPOVAttrib_Left, 1.0f);
        options.SetFloat(kPOVAttrib_Top, 1.0f);
        options.SetFloat(kPOVAttrib_Right, POVMSFloat(width));
        options.SetFloat(kPOVAttrib_Bottom, POVMSFloat(height));
    }
    if (output.Exist(kPOVAttrib_Left))
        for (POVMSType edge : { kPOVAttrib_Left, kPOVAttrib_Top, kPOVAttrib_Right, kPOVAttrib_Bottom })
            options.SetFloat(edge, output.GetFloat(edge));
    if (options.TryGetBool(kPOVAttrib_OutputToFile, true))
        options.SetUCS2String(kPOVAttrib_OutputFile, files[position].c_str());
    return options;
}

}
// end of namespace pov_frontend
