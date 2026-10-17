//******************************************************************************
///
/// @file core/lighting/lightgroup.cpp
///
/// Implements light group utility functions.
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
#include "core/lighting/lightgroup.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <unordered_set>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/lighting/lightsource.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{


namespace
{

bool GivesLight(ConstObjectPtr child)
{
    return ((child->Type & LIGHT_GROUP_LIGHT_OBJECT) == LIGHT_GROUP_LIGHT_OBJECT) &&
           !reinterpret_cast<const LightSource *>(child)->Photon_Only;
}

// Every object in a group without lights from a group inside it takes these lights; those with them add these.
void Give_Group_Lights(CompoundObject *object, const std::vector<LightSource *>& lights, std::unordered_set<ConstObjectPtr>& lit)
{
    for (ObjectPtr child : object->children)
    {
        if (lit.count(child) != 0)
        {
            child->LLights.insert(child->LLights.end(), lights.begin(), lights.end());
            continue;
        }
        child->LLights = lights;
        lit.insert(child);
        if ((child->Type & IS_COMPOUND_OBJECT) == IS_COMPOUND_OBJECT)
            Give_Group_Lights(reinterpret_cast<CompoundObject *>(child), lights, lit);
    }
}

}

bool Light_Group_Lights_Something(const CSG *group)
{
    bool light = false, object = false;
    for (ConstObjectPtr child : group->children)
    {
        if ((child->Type & LIGHT_GROUP_LIGHT_OBJECT) == LIGHT_GROUP_LIGHT_OBJECT)
            light = light || GivesLight(child);
        else
            object = true;
    }
    return light && object;
}

void Assign_Light_Groups(ObjectPtr object, std::unordered_set<ConstObjectPtr>& lit)
{
    if ((object->Type & IS_COMPOUND_OBJECT) != IS_COMPOUND_OBJECT)
        return;
    CompoundObject *compound = reinterpret_cast<CompoundObject *>(object);
    for (ObjectPtr child : compound->children)
        Assign_Light_Groups(child, lit);
    CSG *group = dynamic_cast<CSG *>(object);
    if ((group == nullptr) || ((object->Type & LIGHT_GROUP_OBJECT) == 0) || !group->lightGroupActive)
        return;
    std::vector<LightSource *> lights;
    for (ObjectPtr child : compound->children)
        if (GivesLight(child))
            lights.push_back(reinterpret_cast<LightSource *>(child));
    Give_Group_Lights(compound, lights, lit);
    if (group->lightGroupCopy)
        return;
    object->LLights = lights;
    lit.insert(object);
}


/*****************************************************************************
*
* FUNCTION
*
*   Check_Photon_Light_Group
*
* INPUT
*
*   Object - any object
*
* OUTPUT
*
* RETURNS
*
*   True if this object is lit by the photon light (according to the light_group rules)
*
*
* AUTHOR
*
*   Nathan Kopp [NK]
*
* DESCRIPTION
*
*   If the photon light is a global light (not in a light group) as determined by
*   the photonOptions object, then we just check to see if the object interacts
*   with global lights.
*
*   Otherwise...
*
*   Checks to see if Light is one of Object's local lights (part of the light
*   group).
*
* CHANGES
*
*   Apr 2002 : Creation.
*
******************************************************************************/

bool Check_Photon_Light_Group(ConstObjectPtr Object)
{
/* TODO FIXME   if(photonOptions.Light_Is_Global)
    {
        if((Object->Flags & NO_GLOBAL_LIGHTS_FLAG) == NO_GLOBAL_LIGHTS_FLAG)
            return false;
        else
            return true;
    }
    else
    {
        for(vector<LightSource *>::iterator Test_Light = Object->LLights.begin(); Test_Light != Object->LLights.end(); Test_Light++)
        {
            if(*Test_Light == photonOptions.Light)
                return true;
        } */
        return true;
//  }
}

}
// end of namespace pov
