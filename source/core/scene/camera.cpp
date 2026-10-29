//******************************************************************************
///
/// @file core/scene/camera.cpp
///
/// Implementations related to cameras.
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
#include "core/scene/camera.h"

// C++ variants of C standard header files
// C++ standard header files
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/lighting/lightsource.h"
#include "core/material/normal.h"
#include "core/material/pigment.h"
#include "core/math/matrix.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

/*****************************************************************************
*
* FUNCTION
*
*   Translate_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void Camera::Translate(const Vector3d& Vector)
{
    Location += Vector;
}



/*****************************************************************************
*
* FUNCTION
*
*   Rotate_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void Camera::Rotate(const Vector3d& Vector)
{
    TRANSFORM Trans;

    Compute_Rotation_Transform(&Trans, Vector);
    Transform(&Trans);
}



/*****************************************************************************
*
* FUNCTION
*
*   Scale_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void Camera::Scale(const Vector3d& Vector)
{
    TRANSFORM Trans;

    Compute_Scaling_Transform(&Trans, Vector);
    Transform(&Trans);
}



/*****************************************************************************
*
* FUNCTION
*
*   Transform_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void Camera::Transform(const TRANSFORM *Trans)
{
    MTransPoint(Location, Location, Trans);
    MTransDirection(Direction, Direction, Trans);
    MTransDirection(Up, Up, Trans);
    MTransDirection(Right, Right, Trans);
}



/*****************************************************************************
*
* METHOD
*
*   Camera::Init
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void Camera::Init()
{
    Location    = Vector3d(0.0,  0.0, 0.0);
    Direction   = Vector3d(0.0,  0.0, 1.0);
    Up          = Vector3d(0.0,  1.0, 0.0);
    Right       = Vector3d(1.33, 0.0, 0.0); // TODO FIXME
    Sky         = Vector3d(0.0,  1.0, 0.0);
    Look_At     = Vector3d(0.0,  0.0, 1.0);
    Focal_Point = Vector3d(0.0,  0.0, 1.0);

    /* Init focal blur stuff (not used by default). */
    Blur_Samples        = 0;
    Blur_Samples_Min    = 0;
    Confidence          = 0.9;
    Variance            = 1.0 / 10000.0;
    Aperture            = 0.0;
    Focal_Distance      = -1.0;

    /* Set default camera type and viewing angle. [DB 7/94] */
    Type = PERSPECTIVE_CAMERA;
    Angle = 90.0;

    /* Default view angle for spherical camera. [MH 6/99] */
    H_Angle = 360;
    V_Angle = 180;

    /* Do not perturb primary rays by default. [DB 7/94] */
    Tnormal = nullptr;

    Bokeh = nullptr; // no user-defined bokeh by default

    Trans = Create_Transform();

    Rays_Per_Pixel = 1;
    Face_Distribution_Method = 0;
    Smooth = false;
    Max_Ray_Distance = 0.0;
    Radiosity_Width = 0;
    Radiosity_Height = 0;
    No_Radiosity = false;
    output = Output();
    aspectRole = AspectRole::Fixed;
    aspect = 0.0;
    focalDefault = false;
    tags.clear();
    filterTags = TagFilter();

    for (unsigned int i = 0; i < 3; ++i)
    {
        Location_Fn[i]  = nullptr;
        Direction_Fn[i] = nullptr;
    }
}

/*****************************************************************************
*
* FUNCTION
*
*   Create_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

Camera::Camera()
{
    Init();
}



/*****************************************************************************
*
* FUNCTION
*
*   Copy_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   POV-Ray Team
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

Camera& Camera::operator=(const Camera& src)
{
    Location    = src.Location;
    Direction   = src.Direction;
    Up          = src.Up;
    Right       = src.Right;
    Sky         = src.Sky;
    Look_At     = src.Look_At;
    Focal_Point = src.Focal_Point;

    Focal_Distance = src.Focal_Distance;
    Aperture = src.Aperture;
    Blur_Samples = src.Blur_Samples;
    Blur_Samples_Min = src.Blur_Samples_Min;
    Confidence = src.Confidence;
    Variance = src.Variance;
    Type = src.Type;
    Angle = src.Angle;
    H_Angle = src.H_Angle;
    V_Angle = src.V_Angle;

    if (Tnormal != nullptr)
        Destroy_Tnormal(Tnormal);
    Tnormal = (src.Tnormal ? Copy_Tnormal(src.Tnormal) : nullptr);
    if (Trans != nullptr)
        Destroy_Transform(Trans);
    Trans = (src.Trans ? Copy_Transform(src.Trans) : nullptr);

    if (Bokeh != nullptr)
        Destroy_Pigment(Bokeh);
    Bokeh = (src.Bokeh ? Copy_Pigment(src.Bokeh) : nullptr);

    for (std::vector<ObjectPtr>::iterator it = Meshes.begin(); it != Meshes.end(); it++)
        Destroy_Object(*it);
    Meshes.clear();
    for (std::vector<ObjectPtr>::const_iterator it = src.Meshes.begin(); it != src.Meshes.end(); it++)
        Meshes.push_back(Copy_Object(*it));
    Face_Distribution_Method = src.Face_Distribution_Method;
    Rays_Per_Pixel = src.Rays_Per_Pixel;
    Max_Ray_Distance = src.Max_Ray_Distance;
    Radiosity_Width = src.Radiosity_Width;
    Radiosity_Height = src.Radiosity_Height;
    No_Radiosity = src.No_Radiosity;
    output = src.output;
    aspectRole = src.aspectRole;
    aspect = src.aspect;
    focalDefault = src.focalDefault;
    tags = src.tags;
    filterTags = src.filterTags;
    frameBounds = src.frameBounds;
    frameTarget = src.frameTarget;
    frameSelection = src.frameSelection;
    frameSource = src.frameSource;
    frameLow = src.frameLow;
    frameHigh = src.frameHigh;
    framePlacement = src.framePlacement;
    frameMultiplier = src.frameMultiplier;
    windowLow[0] = src.windowLow[0]; windowLow[1] = src.windowLow[1];
    windowHigh[0] = src.windowHigh[0]; windowHigh[1] = src.windowHigh[1];
    Mesh_Index = src.Mesh_Index;
    for (int i = 0; i < 10; i++)
    {
        U_Xref[i] = src.U_Xref[i];
        V_Xref[i] = src.V_Xref[i];
    }
    Smooth = src.Smooth;

    for (unsigned int i = 0; i < 3; ++i)
    {
        if (Location_Fn[i] != nullptr)
            delete Location_Fn[i];
        if (src.Location_Fn[i] == nullptr)
            Location_Fn[i] = nullptr;
        else
            Location_Fn[i] = src.Location_Fn[i]->Clone();

        if (Direction_Fn[i] != nullptr)
            delete Direction_Fn[i];
        if (src.Direction_Fn[i] == nullptr)
            Direction_Fn[i] = nullptr;
        else
            Direction_Fn[i] = src.Direction_Fn[i]->Clone();

    }

    return *this;
}


bool FiniteBounds(const Vector3d& low, const Vector3d& high)
{
    for (unsigned int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || (low[axis] > high[axis]) ||
            !std::isfinite(high[axis] - low[axis]))
            return false;
    return true;
}

static void LightBounds(const LightSource *light, Vector3d& low, Vector3d& high)
{
    low = high = light->Center;
    if (!light->Area_Light)
        return;
    if (light->Orient)
    {
        const DBL radius = 0.5 * std::max(light->Axis1.length(), light->Axis2.length());
        low -= Vector3d(radius);
        high += Vector3d(radius);
        return;
    }
    for (int a = -1; a <= 1; a += 2)
        for (int b = -1; b <= 1; b += 2)
        {
            const Vector3d corner = light->Center + light->Axis1 * (0.5 * a) + light->Axis2 * (0.5 * b);
            low = min(low, corner);
            high = max(high, corner);
        }
}

bool FrameTargetBounds(ConstObjectPtr object, Vector3d& low, Vector3d& high, unsigned int& unbounded)
{
    const CompoundObject *compound = dynamic_cast<const CompoundObject *>(object);
    if (const LightSource *light = dynamic_cast<const LightSource *>(object))
    {
        // a light frames as its point or area, together with its looks_like object
        LightBounds(light, low, high);
        for (ConstObjectPtr child : compound->children)
        {
            Vector3d childLow, childHigh;
            if (FrameTargetBounds(child, childLow, childHigh, unbounded))
            {
                low = min(low, childLow);
                high = max(high, childHigh);
            }
        }
        return FiniteBounds(low, high);
    }
    Make_min_max_from_BBox(low, high, object->BBox);
    if (object->BBox.finite && FiniteBounds(low, high))
        return true;
    if (compound == nullptr)
    {
        ++unbounded;
        return false;
    }
    const bool intersection = (dynamic_cast<const CSGIntersection *>(object) != nullptr);
    bool found = false;
    unsigned int childUnbounded = 0;
    low = Vector3d(intersection ? -HUGE_VAL : HUGE_VAL);
    high = Vector3d(intersection ? HUGE_VAL : -HUGE_VAL);
    for (ConstObjectPtr child : compound->children)
    {
        Vector3d childLow, childHigh;
        // Only finite interior bounds constrain an intersection.
        if ((intersection && (!child->BBox.finiteInterior || Test_Flag(child, INVERTED_FLAG))) ||
            !FrameTargetBounds(child, childLow, childHigh, childUnbounded))
            continue;
        for (unsigned int axis = 0; axis < 3; ++axis)
        {
            low[axis] = intersection ? std::max(low[axis], childLow[axis]) : std::min(low[axis], childLow[axis]);
            high[axis] = intersection ? std::min(high[axis], childHigh[axis]) : std::max(high[axis], childHigh[axis]);
        }
        found = true;
    }
    // An intersection's unbounded children only stop constraining it.
    if (!intersection)
        unbounded += childUnbounded;
    else if (!found)
        ++unbounded;
    return found && FiniteBounds(low, high);
}

std::vector<FrameBounds> FrameTargetParts(ConstObjectPtr object)
{
    std::vector<FrameBounds> parts;
    std::function<void(ConstObjectPtr)> collect = [&](ConstObjectPtr target) {
        if (const CSGUnion *group = dynamic_cast<const CSGUnion *>(target))
        {
            if (!Test_Flag(target, INVERTED_FLAG))
            {
                for (ConstObjectPtr child : group->children) collect(child);
                return;
            }
        }
        FrameBounds bounds;
        unsigned int unbounded = 0;
        if (FrameTargetBounds(target, bounds.low, bounds.high, unbounded)) parts.push_back(bounds);
    };
    collect(object);
    return parts;
}

// Bounds too small beside their distance to define a fit: a light's point, or a single point.
static bool PointBounds(const Vector3d& low, const Vector3d& high, const Vector3d& from)
{
    return (high - low).length() <= 1e-12 * ((low + high) * 0.5 - from).length();
}

static bool ProjectPoint(const Camera& camera, const Vector3d& point, DBL& x, DBL& y)
{
    const DBL tiny = 1e-9;
    Vector3d right = camera.Right, up = camera.Up, forward = camera.Direction;
    const DBL rightLength = right.length(), upLength = up.length(), forwardLength = forward.length();
    if ((rightLength <= 0.0) || (upLength <= 0.0) || (forwardLength <= 0.0))
        return false;
    const bool normalized = (camera.Type != PERSPECTIVE_CAMERA) && (camera.Type != ORTHOGRAPHIC_CAMERA) &&
                            (camera.Type != SPHERICAL_CAMERA);
    if (normalized)
    {
        right.normalize();
        up.normalize();
        forward.normalize();
    }
    const DBL determinant = dot(right, cross(up, forward));
    if (fabs(determinant) <= tiny * right.length() * up.length() * forward.length())
        return false;
    const Vector3d offset = point - camera.Location;
    const DBL distance = offset.length();
    if (distance <= 0.0) return false;
    const DBL a = dot(offset, cross(up, forward)) / determinant;
    const DBL b = dot(offset, cross(forward, right)) / determinant;
    const DBL c = dot(offset, cross(right, up)) / determinant;
    const DBL angle = camera.Angle * M_PI_180;
    switch (camera.Type)
    {
        case PERSPECTIVE_CAMERA:
            if (c * forwardLength <= tiny * distance) return false;
            x = a / c;
            y = b / c;
            break;
        case ORTHOGRAPHIC_CAMERA:
            if (c <= 0.0) return false;
            x = a;
            y = b;
            break;
        case CYL_1_CAMERA:
        case CYL_3_CAMERA:
            if ((angle <= 0.0) || (hypot(a, c) <= tiny * distance)) return false;
            x = atan2(a, c) / angle;
            y = b / ((camera.Type == CYL_1_CAMERA) ? hypot(a, c) * upLength : upLength);
            break;
        case CYL_2_CAMERA:
        case CYL_4_CAMERA:
            if ((angle <= 0.0) || (hypot(b, c) <= tiny * distance)) return false;
            x = a / ((camera.Type == CYL_2_CAMERA) ? hypot(b, c) * rightLength : rightLength);
            y = atan2(b, c) / angle;
            break;
        case SPHERICAL_CAMERA:
            if ((camera.H_Angle <= 0.0) || (camera.V_Angle <= 0.0)) return false;
            x = atan2(a * rightLength, c * forwardLength) / (camera.H_Angle * M_PI_180);
            y = atan2(b * upLength, hypot(a * rightLength, c * forwardLength)) / (camera.V_Angle * M_PI_180);
            break;
        case FISHEYE_CAMERA:
        {
            if (angle <= 0.0) return false;
            const DBL radial = hypot(a, b);
            const DBL radius = 2.0 * atan2(radial, c) / angle;
            x = (radial > tiny * distance) ? radius * a / radial / (2.0 * rightLength) : 0.0;
            y = (radial > tiny * distance) ? radius * b / radial / (2.0 * upLength) : 0.0;
            break;
        }
        case ULTRA_WIDE_ANGLE_CAMERA:
        {
            // Solves sin h = a s, sin v = b s, cos h cos v = c s for s, the root with |h|, |v| below 90 degrees.
            if ((angle <= 0.0) || (c <= tiny * distance)) return false;
            const DBL sum = a * a + b * b + c * c;
            const DBL scale = sqrt(2.0 / (sum + sqrt(std::max(0.0, sum * sum - 4.0 * a * a * b * b))));
            x = asin(std::max(-1.0, std::min(1.0, a * scale))) / angle;
            y = asin(std::max(-1.0, std::min(1.0, b * scale))) / (angle * upLength / rightLength);
            break;
        }
        default: return false;
    }
    return std::isfinite(x) && std::isfinite(y);
}

struct BoundsProjection final
{
    DBL left, right, bottom, top, radius;
};

static bool ProjectBounds(const Camera& camera, const Vector3d& low, const Vector3d& high, BoundsProjection& projection)
{
    if (!FiniteBounds(low, high))
        return false;
    projection = { HUGE_VAL, -HUGE_VAL, HUGE_VAL, -HUGE_VAL, 0.0 };
    const DBL rightLength = camera.Right.length(), upLength = camera.Up.length();
    // Perspective and affine extrema occur at vertices; curved projections also inspect the surface.
    const unsigned int steps = ((camera.Type == PERSPECTIVE_CAMERA) || (camera.Type == ORTHOGRAPHIC_CAMERA)) ? 1 : 4;
    for (unsigned int i = 0; i <= steps; ++i)
        for (unsigned int j = 0; j <= steps; ++j)
            for (unsigned int k = 0; k <= steps; ++k)
            {
                if ((i != 0) && (i != steps) && (j != 0) && (j != steps) && (k != 0) && (k != steps)) continue;
                const Vector3d point(low[X] + (high[X] - low[X]) * i / steps,
                                     low[Y] + (high[Y] - low[Y]) * j / steps,
                                     low[Z] + (high[Z] - low[Z]) * k / steps);
                DBL x, y;
                if (!ProjectPoint(camera, point, x, y)) return false;
                projection.left = std::min(projection.left, x);
                projection.right = std::max(projection.right, x);
                projection.bottom = std::min(projection.bottom, y);
                projection.top = std::max(projection.top, y);
                projection.radius = std::max(projection.radius, hypot(x * rightLength, y * upLength));
            }
    return true;
}

// Scales the parsed field by zoom (below 1 narrows it); false for projections without a field to scale.
static bool ApplyZoom(Camera& camera, const Camera& base, DBL zoom)
{
    switch (camera.Type)
    {
        case PERSPECTIVE_CAMERA: camera.Direction = base.Direction / zoom; return true;
        case ORTHOGRAPHIC_CAMERA: camera.Right = base.Right * zoom; camera.Up = base.Up * zoom; return true;
        case FISHEYE_CAMERA:
        case ULTRA_WIDE_ANGLE_CAMERA: camera.Angle = base.Angle * zoom; return true;
        case CYL_1_CAMERA:
        case CYL_3_CAMERA: camera.Angle = base.Angle * zoom; camera.Up = base.Up * zoom; return true;
        case CYL_2_CAMERA:
        case CYL_4_CAMERA: camera.Angle = base.Angle * zoom; camera.Right = base.Right * zoom; return true;
        case SPHERICAL_CAMERA: camera.H_Angle = base.H_Angle * zoom; camera.V_Angle = base.V_Angle * zoom; return true;
        default: return false;
    }
}

bool Camera::FitsFrame(const Vector3d& low, const Vector3d& high) const
{
    BoundsProjection projected;
    if (!ProjectBounds(*this, low, high, projected)) return false;
    const DBL x = std::max(fabs(projected.left), fabs(projected.right)), y = std::max(fabs(projected.bottom), fabs(projected.top));
    const DBL limit = 0.5 * frameMultiplier;
    if ((Type == FISHEYE_CAMERA) && (projected.radius > limit)) return false;
    return (x <= limit) && (y <= limit);
}

void Camera::PlaceFrame(const TRANSFORM *trans)
{
    std::shared_ptr<TRANSFORM> placed = std::make_shared<TRANSFORM>();
    if (framePlacement)
        *placed = *framePlacement;
    else
    {
        MIdentity(placed->matrix);
        MIdentity(placed->inverse);
    }
    Compose_Transforms(placed.get(), trans);
    framePlacement = placed;
}

namespace
{

// The rays of a user_defined camera, evaluated from its functions at a point (u, v) of its output space.
class UserRays final
{
public:
    UserRays(const Camera& camera, TraceThreadData *threadData) : mCamera(camera)
    {
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (camera.Location_Fn[i] != nullptr) mLocation[i].reset(new GenericScalarFunctionInstance(camera.Location_Fn[i], threadData));
            if (camera.Direction_Fn[i] != nullptr) mDirection[i].reset(new GenericScalarFunctionInstance(camera.Direction_Fn[i], threadData));
        }
    }
    bool Ray(DBL u, DBL v, Vector3d& origin, Vector3d& direction)
    {
        origin = mCamera.Location;
        direction = mCamera.Direction;
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (mLocation[i]) origin[i] = mLocation[i]->Evaluate(u, v);
            if (mDirection[i]) direction[i] = mDirection[i]->Evaluate(u, v);
            if (!std::isfinite(origin[i]) || !std::isfinite(direction[i])) return false;
        }
        return direction.lengthSqr() > 0.0;
    }
    bool Hits(DBL u, DBL v, const Vector3d& low, const Vector3d& high)
    {
        Vector3d origin, direction;
        if (!Ray(u, v, origin, direction)) return false;
        DBL near = 0.0, far = HUGE_VAL;
        for (unsigned int axis = 0; axis < 3; ++axis)
        {
            if (direction[axis] == 0.0)
            {
                if ((origin[axis] < low[axis]) || (origin[axis] > high[axis])) return false;
                continue;
            }
            DBL a = (low[axis] - origin[axis]) / direction[axis], b = (high[axis] - origin[axis]) / direction[axis];
            if (a > b) std::swap(a, b);
            near = std::max(near, a);
            far = std::min(far, b);
            if (near > far) return false;
        }
        return true;
    }
private:
    const Camera& mCamera;
    std::unique_ptr<GenericScalarFunctionInstance> mLocation[3], mDirection[3];
};

}

FrameResult Camera::FrameWindow(const Vector3d& low, const Vector3d& high, TraceThreadData *threadData,
                                const std::function<void()>& cooperate, const std::vector<FrameBounds>& parts)
{
    if (!FiniteBounds(low, high) || (threadData == nullptr)) return FrameResult::Failed;
    UserRays rays(*this, threadData);
    auto hits = [&](DBL u, DBL v) {
        if (parts.empty()) return rays.Hits(u, v, low, high);
        for (const FrameBounds& part : parts)
            if (rays.Hits(u, v, part.low, part.high)) return true;
        return false;
    };
    const DBL u0 = windowLow[0], v0 = windowLow[1], spanU = windowHigh[0] - u0, spanV = windowHigh[1] - v0;
    // Azimuth wraps when the rays at both side edges coincide, as in a 360 degree cylindrical projection.
    bool circular = true;
    for (unsigned int k = 0; (k < 5) && circular; ++k)
    {
        Vector3d originA, directionA, originB, directionB;
        const DBL v = v0 + (k + 0.5) / 5.0 * spanV;
        circular = rays.Ray(u0, v, originA, directionA) && rays.Ray(u0 + spanU, v, originB, directionB) &&
                   ((originA - originB).length() <= 1e-9 * (1.0 + originA.length())) &&
                   (dot(directionA.normalized(), directionB.normalized()) >= 1.0 - 1e-12);
    }
    const unsigned int columns = 256, rows = 128;
    const DBL du = spanU / columns, dv = spanV / rows;
    std::vector<bool> columnHit(columns, false), rowHit(rows, false);
    const Vector3d centre = (low + high) * 0.5;
    DBL bestU = u0 + spanU * 0.5, bestV = v0 + spanV * 0.5, bestCos = -2.0;
    bool any = false;
    for (unsigned int i = 0; i < columns; ++i)
    {
        if (cooperate) cooperate();
        for (unsigned int j = 0; j < rows; ++j)
        {
            const DBL u = u0 + (i + 0.5) * du, v = v0 + (j + 0.5) * dv;
            Vector3d origin, direction;
            if (!rays.Ray(u, v, origin, direction)) continue;
            const DBL cosine = dot(direction.normalized(), (centre - origin).normalized());
            if (cosine > bestCos) { bestCos = cosine; bestU = u; bestV = v; }
            if (hits(u, v)) { columnHit[i] = rowHit[j] = any = true; }
        }
    }
    auto place = [&](DBL scale, DBL centreU, DBL centreV) {
        const DBL halfU = spanU * scale * 0.5, halfV = spanV * scale * 0.5;
        centreV = std::max(v0 + halfV, std::min(v0 + spanV - halfV, centreV));
        if (circular)
            centreU = u0 + spanU * 0.5 + remainder(centreU - (u0 + spanU * 0.5), spanU);
        else
            centreU = std::max(u0 + halfU, std::min(u0 + spanU - halfU, centreU));
        windowLow[0] = centreU - halfU;
        windowHigh[0] = centreU + halfU;
        windowLow[1] = centreV - halfV;
        windowHigh[1] = centreV + halfV;
    };
    if (PointBounds(low, high, Location))
    {
        // A point target: the ray closest to it, found by walking from the best grid ray, centres the unnarrowed window.
        DBL stepU = du, stepV = dv;
        for (unsigned int iteration = 0; (iteration < 400) && (stepU > spanU * 1e-12); ++iteration)
        {
            bool moved = false;
            const DBL fromU = bestU, fromV = bestV;
            for (int a = -1; a <= 1; ++a)
                for (int b = -1; b <= 1; ++b)
                {
                    const DBL u = fromU + a * stepU, v = std::max(v0, std::min(v0 + spanV, fromV + b * stepV));
                    if (!circular && ((u < u0) || (u > u0 + spanU))) continue;
                    Vector3d origin, direction;
                    if (!rays.Ray(u, v, origin, direction)) continue;
                    const DBL cosine = dot(direction.normalized(), (centre - origin).normalized());
                    if (cosine > bestCos) { bestCos = cosine; bestU = u; bestV = v; moved = true; }
                }
            if (!moved) { stepU *= 0.5; stepV *= 0.5; }
        }
        if (bestCos < -1.5) return FrameResult::Failed;
        place(1.0, bestU, bestV);
        return FrameResult::Pointed;
    }
    DBL lowU, highU, lowV, highV;
    if (any)
    {
        lowV = HUGE_VAL; highV = -HUGE_VAL;
        for (unsigned int j = 0; j < rows; ++j)
            if (rowHit[j]) { lowV = std::min(lowV, v0 + (j + 0.5) * dv); highV = std::max(highV, v0 + (j + 0.5) * dv); }
        unsigned int gapStart = 0, gapLength = 0;
        if (circular)
            for (unsigned int i = 0; i < columns; ++i)
            {
                unsigned int length = 0;
                while ((length < columns) && !columnHit[(i + length) % columns]) ++length;
                if (length > gapLength) { gapLength = length; gapStart = i; }
            }
        if (circular && (gapLength > 0))
        {
            const unsigned int first = (gapStart + gapLength) % columns, last = (gapStart + columns - 1) % columns;
            lowU = u0 + (first + 0.5) * du;
            highU = u0 + (last + 0.5) * du;
            if (highU < lowU) highU += spanU;
        }
        else
        {
            lowU = HUGE_VAL; highU = -HUGE_VAL;
            for (unsigned int i = 0; i < columns; ++i)
                if (columnHit[i]) { lowU = std::min(lowU, u0 + (i + 0.5) * du); highU = std::max(highU, u0 + (i + 0.5) * du); }
        }
    }
    else
    {
        // A target narrower than the grid: walk from the ray that points closest to its centre until one hits.
        DBL stepU = du, stepV = dv;
        bool found = false;
        for (unsigned int iteration = 0; (iteration < 400) && !found && (stepU > spanU * 1e-12); ++iteration)
        {
            bool moved = false;
            for (int a = -1; (a <= 1) && !found; ++a)
                for (int b = -1; (b <= 1) && !found; ++b)
                {
                    const DBL u = bestU + a * stepU, v = std::max(v0, std::min(v0 + spanV, bestV + b * stepV));
                    if (!circular && ((u < u0) || (u > u0 + spanU))) continue;
                    Vector3d origin, direction;
                    if (!rays.Ray(u, v, origin, direction)) continue;
                    if (rays.Hits(u, v, low, high)) { bestU = u; bestV = v; found = true; break; }
                    const DBL cosine = dot(direction.normalized(), (centre - origin).normalized());
                    if (cosine > bestCos) { bestCos = cosine; bestU = u; bestV = v; moved = true; }
                }
            if (!moved) { stepU *= 0.5; stepV *= 0.5; }
        }
        if (!found) return FrameResult::Failed;
        lowU = highU = bestU;
        lowV = highV = bestV;
    }
    // Moves each edge out to where the rays stop hitting, testing a line of rays across the other axis.
    const unsigned int samples = 33;
    auto lineHits = [&](bool alongU, DBL at, DBL from, DBL to) {
        for (unsigned int k = 0; k < samples; ++k)
        {
            const DBL across = from + (to - from) * k / (samples - 1.0);
            if (alongU ? hits(at, across) : hits(across, at)) return true;
        }
        return false;
    };
    auto refine = [&](bool alongU, DBL inner, DBL sign, DBL step, DBL limit, bool bounded, DBL from, DBL to) {
        DBL outer = inner + sign * step;
        while (true)
        {
            if (bounded && ((outer - limit) * sign >= 0.0))
            {
                if (lineHits(alongU, limit, from, to)) return limit;
                outer = limit;
                break;
            }
            if (!lineHits(alongU, outer, from, to)) break;
            inner = outer;
            step *= 2.0;
            outer = inner + sign * step;
            if (fabs(outer - inner) > (alongU ? spanU : spanV)) return inner;
        }
        for (unsigned int i = 0; i < 40; ++i)
        {
            const DBL middle = (inner + outer) * 0.5;
            if (lineHits(alongU, middle, from, to)) inner = middle;
            else outer = middle;
        }
        return inner;
    };
    const bool fullCircle = circular && any && (lowU <= u0 + du) && (highU >= u0 + spanU - du) && (highU - lowU >= spanU - 1.5 * du);
    for (unsigned int pass = 0; pass < 2; ++pass)
    {
        const DBL acrossLowV = std::max(v0, lowV - dv), acrossHighV = std::min(v0 + spanV, highV + dv);
        if (!fullCircle)
        {
            const DBL newLowU = refine(true, lowU, -1.0, du * 0.5, u0, !circular, acrossLowV, acrossHighV);
            const DBL newHighU = refine(true, highU, 1.0, du * 0.5, u0 + spanU, !circular, acrossLowV, acrossHighV);
            lowU = newLowU;
            highU = newHighU;
        }
        else
        {
            lowU = u0;
            highU = u0 + spanU;
        }
        const DBL acrossLowU = fullCircle ? u0 : (circular ? lowU - du : std::max(u0, lowU - du));
        const DBL acrossHighU = fullCircle ? u0 + spanU : (circular ? highU + du : std::min(u0 + spanU, highU + du));
        lowV = refine(false, lowV, -1.0, dv * 0.5, v0, true, acrossLowU, acrossHighU);
        highV = refine(false, highV, 1.0, dv * 0.5, v0 + spanV, true, acrossLowU, acrossHighU);
    }
    // The window keeps the parsed pixel aspect and grows from the target until it fills frameMultiplier of it.
    DBL scale = std::max((highU - lowU) / (frameMultiplier * spanU), (highV - lowV) / (frameMultiplier * spanV));
    scale = std::max(scale, 1e-9);
    FrameResult result = FrameResult::Fitted;
    if (scale > 1.0) { scale = 1.0; result = FrameResult::Aimed; }
    place(scale, (lowU + highU) * 0.5, (lowV + highV) * 0.5);
    return result;
}

FrameResult Camera::Frame(const Vector3d& low, const Vector3d& high, TraceThreadData *threadData, const std::function<void()>& cooperate, const std::vector<FrameBounds>& parts)
{
    const FrameResult result = (Type == USER_DEFINED_CAMERA) ? FrameWindow(low, high, threadData, cooperate, parts) : FrameField(low, high, parts);
    if (Focal_Distance < 0.0)
    {
        Vector3d direction = Direction;
        if (framePlacement) MInvTransDirection(direction, Direction, framePlacement.get());
        Focal_Distance = direction.length();
    }
    if (Focal_Distance == 0.0)
        Focal_Distance = 1.0;
    return result;
}

FrameResult Camera::FrameField(const Vector3d& low, const Vector3d& high, const std::vector<FrameBounds>& parts)
{
    if (!FiniteBounds(low, high) || (Direction.lengthSqr() <= 0.0) || (Type == MESH_CAMERA) || (Type == USER_DEFINED_CAMERA))
        return FrameResult::Failed;
    const DBL extent = (high - low).length();
    Vector3d center = (low + high) * 0.5, location = Location, direction = Direction, up = Up, rightVector = Right;
    if (framePlacement)
    {
        MInvTransPoint(center, center, framePlacement.get());
        MInvTransPoint(location, Location, framePlacement.get());
        MInvTransDirection(direction, Direction, framePlacement.get());
        MInvTransDirection(up, Up, framePlacement.get());
        MInvTransDirection(rightVector, Right, framePlacement.get());
    }
    if (parts.size() > 1 && Type != ORTHOGRAPHIC_CAMERA)
    {
        Vector3d angularLow(HUGE_VAL), angularHigh(-HUGE_VAL);
        for (const FrameBounds& part : parts)
            for (unsigned int corner = 0; corner < 8; ++corner)
            {
                Vector3d point((corner & 1) ? part.high[X] : part.low[X],
                               (corner & 2) ? part.high[Y] : part.low[Y],
                               (corner & 4) ? part.high[Z] : part.low[Z]);
                if (framePlacement) MInvTransPoint(point, point, framePlacement.get());
                const Vector3d ray = point - location;
                if (ray.lengthSqr() == 0.0) continue;
                angularLow = min(angularLow, ray.normalized());
                angularHigh = max(angularHigh, ray.normalized());
            }
        const Vector3d aim = (angularLow + angularHigh) * 0.5;
        if (aim.lengthSqr() > 1e-20) center = location + aim.normalized() * direction.length();
    }
    const Vector3d toCenter = center - location;
    Vector3d forward = (toCenter.length() > ((parts.size() > 1 && Type != ORTHOGRAPHIC_CAMERA) ? 0.0 : 1e-9 * extent))
        ? toCenter.normalized() : direction.normalized();
    if (parts.size() > 1 && Type != ORTHOGRAPHIC_CAMERA)
    {
        for (unsigned int pass = 0; pass < 32; ++pass)
        {
            Vector3d horizontal = cross(Sky, forward);
            if (horizontal.lengthSqr() <= 1e-20) break;
            horizontal.normalize();
            const Vector3d vertical = cross(forward, horizontal).normalized();
            DBL left = HUGE_VAL, right = -HUGE_VAL, bottom = HUGE_VAL, top = -HUGE_VAL;
            bool front = true;
            for (const FrameBounds& part : parts)
                for (unsigned int corner = 0; corner < 8; ++corner)
                {
                    Vector3d point((corner & 1) ? part.high[X] : part.low[X],
                                   (corner & 2) ? part.high[Y] : part.low[Y],
                                   (corner & 4) ? part.high[Z] : part.low[Z]);
                    if (framePlacement) MInvTransPoint(point, point, framePlacement.get());
                    const Vector3d ray = point - location;
                    const DBL depth = dot(ray, forward);
                    if (depth <= 0.0) { front = false; continue; }
                    const DBL x = atan2(dot(ray, horizontal), depth), y = atan2(dot(ray, vertical), depth);
                    left = std::min(left, x); right = std::max(right, x);
                    bottom = std::min(bottom, y); top = std::max(top, y);
                }
            if (!front) break;
            const DBL yaw = (left + right) * 0.5, pitch = (bottom + top) * 0.5;
            if (std::max(fabs(yaw), fabs(pitch)) < 1e-12) break;
            forward = (forward + horizontal * tan(yaw) + vertical * tan(pitch)).normalized();
        }
        center = location + forward * direction.length();
    }
    const DBL directionLength = direction.length(), rightLength = rightVector.length(), upLength = up.length();
    const DBL handedness = dot(cross(up, direction), rightVector);
    Vector3d right = cross(Sky, forward);
    if (right.length() <= 1e-9 * Sky.length()) right = rightVector - forward * dot(rightVector, forward);
    if (right.length() <= 1e-9 * rightLength) return FrameResult::Failed;
    right.normalize();
    up = cross(forward, right).normalized() * upLength;
    rightVector = right * ((handedness > 0.0) ? rightLength : -rightLength);
    direction = forward * directionLength;
    Look_At = center;
    if (framePlacement) MTransPoint(Look_At, center, framePlacement.get());
    Direction = direction;
    Up = up;
    Right = rightVector;
    if (framePlacement)
    {
        MTransDirection(Direction, direction, framePlacement.get());
        MTransDirection(Up, up, framePlacement.get());
        MTransDirection(Right, rightVector, framePlacement.get());
    }
    if (PointBounds(low, high, Location))
        return FrameResult::Pointed;
    auto fits = [&]() {
        if (parts.empty()) return FitsFrame(low, high);
        for (const FrameBounds& part : parts)
            if (!FitsFrame(part.low, part.high)) return false;
        return true;
    };
    const Camera base(*this);
    if (!ApplyZoom(*this, base, 1.0)) return FrameResult::Aimed;
    BoundsProjection projected;
    if (parts.empty())
    {
        if (!ProjectBounds(*this, low, high, projected)) return FrameResult::Aimed;
    }
    else
        for (const FrameBounds& part : parts)
            if (!ProjectBounds(*this, part.low, part.high, projected)) return FrameResult::Aimed;
    DBL upper = 1.0;
    if (!fits())
    {
        if (Type != ORTHOGRAPHIC_CAMERA) return FrameResult::Aimed;
        unsigned int doublings = 0;
        do
        {
            if (++doublings > 64) { ApplyZoom(*this, base, 1.0); return FrameResult::Failed; }
            upper *= 2.0;
            ApplyZoom(*this, base, upper);
        }
        while (!fits());
    }
    DBL lower = upper * 1e-15;
    for (unsigned int i = 0; i < 80; ++i)
    {
        const DBL zoom = sqrt(lower * upper);
        ApplyZoom(*this, base, zoom);
        if (fits()) upper = zoom;
        else lower = zoom;
    }
    ApplyZoom(*this, base, upper);
    return FrameResult::Fitted;
}

static bool SameVector(const Vector3d& a, const Vector3d& b)
{
    return (a.x() == b.x()) && (a.y() == b.y()) && (a.z() == b.z());
}

bool Camera::SameView(const Camera& other, bool compareFilters) const
{
    for (unsigned int i = 0; i < 3; ++i)
        if ((Location_Fn[i] != other.Location_Fn[i]) || (Direction_Fn[i] != other.Direction_Fn[i]))
            return false;
    return SameVector(Location, other.Location) && SameVector(Direction, other.Direction) && SameVector(Up, other.Up) &&
           SameVector(Right, other.Right) && SameVector(Sky, other.Sky) && (Type == other.Type) && (Angle == other.Angle) &&
           (H_Angle == other.H_Angle) && (V_Angle == other.V_Angle) && (Aperture == other.Aperture) &&
           (Focal_Distance == other.Focal_Distance) && (Blur_Samples == other.Blur_Samples) &&
           (Blur_Samples_Min == other.Blur_Samples_Min) && (Confidence == other.Confidence) && (Variance == other.Variance) &&
           (Max_Ray_Distance == other.Max_Ray_Distance) && (Tnormal == other.Tnormal) && (Bokeh == other.Bokeh) &&
           (!compareFilters || (filterTags == other.filterTags)) && (frameBounds == other.frameBounds) &&
           (frameMultiplier == other.frameMultiplier) && (windowLow[0] == other.windowLow[0]) &&
           (windowLow[1] == other.windowLow[1]) && (windowHigh[0] == other.windowHigh[0]) && (windowHigh[1] == other.windowHigh[1]) && (!frameBounds || ((frameTarget == other.frameTarget) && (frameTarget ||
            (SameVector(frameLow, other.frameLow) && SameVector(frameHigh, other.frameHigh))))) &&
           Meshes.empty() && other.Meshes.empty();
}

Camera::Camera(const Camera& src)
{
    Tnormal = nullptr;
    Trans = nullptr;
    Bokeh = nullptr;
    for (unsigned int i = 0; i < 3; ++i)
    {
        Location_Fn[i]  = nullptr;
        Direction_Fn[i] = nullptr;
    }
    operator=(src);
}

/*****************************************************************************
*
* FUNCTION
*
*   Destroy_Camera
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

Camera::~Camera()
{
    Destroy_Tnormal(Tnormal);
    Destroy_Transform(Trans);
    Destroy_Pigment(Bokeh);
    for (std::vector<ObjectPtr>::iterator it = Meshes.begin(); it != Meshes.end(); it++)
        Destroy_Object(*it);
    Meshes.clear();
    for (unsigned int i = 0; i < 3; ++i)
    {
        if (Location_Fn[i] != nullptr)
            delete Location_Fn[i];
        if (Direction_Fn[i] != nullptr)
            delete Direction_Fn[i];
    }
}

}
// end of namespace pov
