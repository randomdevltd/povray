//******************************************************************************
///
/// @file core/shape/portal.cpp
///
/// Implementations related to the portal object.
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
#include "core/shape/portal.h"

// C++ standard header files
#include <algorithm>
#include <vector>

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/material/normal.h"
#include "core/material/pattern.h"
#include "core/material/pigment.h"
#include "core/render/ray.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/disc.h"
#include "core/shape/triangle.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

Portal::Portal() :
    ObjectBase(BASIC_OBJECT),
    body(nullptr),
    pigment(nullptr),
    fallback(nullptr),
    perturb(nullptr),
    perturbAmount(1.0),
    maxDepth(0),
    exit(false),
    front(true),
    back(false),
    reversed(false),
    lights(true),
    farMouth(true),
    farFront(true),
    farBack(false),
    farLights(true),
    farPigment(nullptr),
    partner(nullptr)
{
    MIdentity(map.matrix);
    MIdentity(map.inverse);
    Type |= HOLDS_PORTAL_OBJECT;
    Set_Flag(this, PORTAL_FLAG);
    Set_Flag(this, NO_SHADOW_FLAG);
}

Portal::~Portal()
{
    Destroy_Object(body);
    Destroy_Pigment(pigment);
    Destroy_Pigment(farPigment);
    Destroy_Pigment(fallback);
    Destroy_Tnormal(perturb);
}

ObjectPtr Portal::Copy()
{
    Portal *New = new Portal();
    New->body = Copy_Object(body);
    New->map = map;
    New->pigment = Copy_Pigment(pigment);
    New->fallback = Copy_Pigment(fallback);
    New->perturb = Copy_Tnormal(perturb);
    New->perturbAmount = perturbAmount;
    New->maxDepth = maxDepth;
    New->exit = exit;
    New->front = front;
    New->back = back;
    New->reversed = reversed;
    New->lights = lights;
    New->farMouth = farMouth;
    New->farFront = farFront;
    New->farBack = farBack;
    New->farLights = farLights;
    New->farPigment = Copy_Pigment(farPigment);
    New->origin = origin;
    return New;
}

bool Portal::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    if (!body->Bound.empty() && !Ray_In_Bound(ray, body->Bound, Thread))
        return false;

    IStack Local_Stack(Thread->stackPool);
    bool found = false;
    if (body->All_Intersections(ray, Local_Stack, Thread))
    {
        for (; Local_Stack->size() > 0; Local_Stack->pop())
        {
            if (Clip.empty() || Point_In_Clip(Local_Stack->top().IPoint, Clip, Thread))
            {
                Local_Stack->top().Csg = this;
                Depth_Stack->push(Local_Stack->top());
                found = true;
            }
        }
    }
    return found;
}

bool Portal::Inside(const Vector3d& point, TraceThreadData *Thread) const
{
    return Inside_Object(point, body, Thread);
}

void Portal::Normal(Vector3d& result, Intersection *isect, TraceThreadData *Thread) const
{
    if (isect->Object != this)
        isect->Object->Normal(result, isect, Thread);
}

void Portal::Translate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Portal::Rotate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Portal::Scale(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Portal::Transform(const TRANSFORM *tr)
{
    Transform_Object(body, tr);

    TRANSFORM undo;
    std::copy(&tr->inverse[0][0], &tr->inverse[0][0] + 16, &undo.matrix[0][0]);
    std::copy(&tr->matrix[0][0], &tr->matrix[0][0] + 16, &undo.inverse[0][0]);
    Compose_Transforms(&undo, &map);
    map = undo;

    Transform_Tpattern(pigment, tr);
    Transform_Tpattern(farPigment, tr);
    Transform_Tpattern(fallback, tr);
    Transform_Tpattern(perturb, tr);
    Compute_BBox();
}

ObjectPtr Portal::Invert()
{
    body = body->Invert();
    return this;
}

void Portal::Compute_BBox()
{
    BBox = body->BBox;
}

Portal *Portal::MakeImage() const
{
    Portal *image = new Portal();
    image->body = Copy_Object(body);
    Transform_Object(image->body, &map);
    std::copy(&map.inverse[0][0], &map.inverse[0][0] + 16, &image->map.matrix[0][0]);
    std::copy(&map.matrix[0][0], &map.matrix[0][0] + 16, &image->map.inverse[0][0]);
    image->pigment = Copy_Pigment(farPigment);
    image->fallback = Copy_Pigment(fallback);
    image->perturb = Copy_Tnormal(perturb);
    Transform_Tpattern(image->pigment, &map);
    Transform_Tpattern(image->fallback, &map);
    Transform_Tpattern(image->perturb, &map);
    image->perturbAmount = perturbAmount;
    image->maxDepth = maxDepth;
    image->exit = exit;
    image->front = farFront;
    image->back = farBack;
    image->reversed = !IsClosedSolid(body);
    const MATRIX& m = map.matrix;
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    // A mirrored triangle takes its normal from its vertices, turning it the other way.
    if ((det < 0.0) && (dynamic_cast<const Triangle *>(body) != nullptr))
        image->reversed = !image->reversed;
    image->lights = farLights;
    image->farMouth = false;
    image->partner = this;
    image->origin = "far mouth of the " + origin;
    image->Flags = Flags;
    if (!Bound.empty())
        image->Bound = Copy_Objects(const_cast<std::vector<ObjectPtr>&>(Bound));
    if (!Clip.empty())
        image->Clip = (Clip != Bound) ? Copy_Objects(const_cast<std::vector<ObjectPtr>&>(Clip)) : image->Bound;
    for (ObjectPtr object : image->Bound)
        Transform_Object(object, &map);
    if (image->Clip != image->Bound)
        for (ObjectPtr object : image->Clip)
            Transform_Object(object, &map);
    image->Compute_BBox();
    return image;
}

double Portal::Chord(const Ray& ray, const Vector3d& entry, TraceThreadData *Thread) const
{
    Ray probe(ray);
    probe.Origin = entry;

    IStack Local_Stack(Thread->stackPool);
    std::vector<double> depths;
    if (body->All_Intersections(probe, Local_Stack, Thread))
        for (; Local_Stack->size() > 0; Local_Stack->pop())
            if (Local_Stack->top().Depth >= MIN_ISECT_DEPTH)
                depths.push_back(Local_Stack->top().Depth);
    std::sort(depths.begin(), depths.end());
    const size_t count = depths.size();

    // A hit leaves the body only where the body does not go on past it, unlike the inner faces of a union.
    for (size_t i = 0; i + 1 < count; ++i)
        if (!Inside_Object(entry + probe.Direction * (0.5 * (depths[i] + depths[i + 1])), body, Thread))
            return depths[i];
    return (count > 0) ? depths[count - 1] : -1.0;
}

LightSource *Portal::LightImage(const LightSource *light) const
{
    LightSource *image = static_cast<LightSource *>(Copy_Object(const_cast<LightSource *>(light)));
    for (ObjectPtr child : image->children)
        Destroy_Object(child);
    image->children.clear();
    if (image->Projected_Through_Object != nullptr)
        Destroy_Object(image->Projected_Through_Object);
    image->Projected_Through_Object = nullptr;
    image->portalImages.clear();

    TRANSFORM back;
    std::copy(&map.inverse[0][0], &map.inverse[0][0] + 16, &back.matrix[0][0]);
    std::copy(&map.matrix[0][0], &map.matrix[0][0] + 16, &back.inverse[0][0]);
    image->Transform(&back);
    image->imageOf = light;
    image->portal = this;
    image->lightGroupLight = true;
    return image;
}

bool IsClosedSolid(ConstObjectPtr object)
{
    if ((object->Type & PATCH_OBJECT) || (dynamic_cast<const NonsolidObject *>(object) != nullptr) ||
        (dynamic_cast<const Disc *>(object) != nullptr))
        return false;
    DBL volume;
    BOUNDS_VOLUME(volume, object->BBox);
    return volume <= BOUND_HUGE;
}

}
// end of namespace pov
