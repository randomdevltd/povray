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
//  (none at the moment)

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/material/normal.h"
#include "core/material/pigment.h"
#include "core/math/matrix.h"
#include "core/scene/object.h"

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
    tags = src.tags;
    filterTags = src.filterTags;
    frameBounds = src.frameBounds;
    frameLow = src.frameLow;
    frameHigh = src.frameHigh;
    frameFilter = src.frameFilter;
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
            (fabs(low[axis]) >= BOUND_HUGE * 0.5) || (fabs(high[axis]) >= BOUND_HUGE * 0.5))
            return false;
    return (high - low).lengthSqr() > 0.0;
}

static bool ProjectPoint(const Camera& camera, const Vector3d& point, DBL& x, DBL& y)
{
    Vector3d right = camera.Right, up = camera.Up, forward = camera.Direction;
    const DBL rightLength = right.length(), upLength = up.length();
    if ((rightLength <= 0.0) || (upLength <= 0.0) || (forward.lengthSqr() <= 0.0))
        return false;
    const bool normalized = (camera.Type != PERSPECTIVE_CAMERA) && (camera.Type != ORTHOGRAPHIC_CAMERA) &&
                            (camera.Type != PANORAMIC_CAMERA) && (camera.Type != SPHERICAL_CAMERA);
    if (normalized)
    {
        right.normalize();
        up.normalize();
        forward.normalize();
    }
    const DBL determinant = dot(right, cross(up, forward));
    if (fabs(determinant) < EPSILON)
        return false;
    const Vector3d offset = point - camera.Location;
    if (offset.lengthSqr() <= EPSILON) return false;
    const DBL a = dot(offset, cross(up, forward)) / determinant;
    const DBL b = dot(offset, cross(forward, right)) / determinant;
    const DBL c = dot(offset, cross(right, up)) / determinant;
    const DBL angle = camera.Angle * M_PI_180;
    switch (camera.Type)
    {
        case PERSPECTIVE_CAMERA:
            if (c <= EPSILON) return false;
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
            if ((angle <= 0.0) || (hypot(a, c) <= EPSILON)) return false;
            x = atan2(a, c) / angle;
            y = b / ((camera.Type == CYL_1_CAMERA) ? hypot(a, c) * upLength : upLength);
            break;
        case CYL_2_CAMERA:
        case CYL_4_CAMERA:
            if ((angle <= 0.0) || (hypot(b, c) <= EPSILON)) return false;
            x = a / ((camera.Type == CYL_2_CAMERA) ? hypot(b, c) * rightLength : rightLength);
            y = atan2(b, c) / angle;
            break;
        case SPHERICAL_CAMERA:
            if ((camera.H_Angle <= 0.0) || (camera.V_Angle <= 0.0)) return false;
            x = atan2(a * rightLength, c * forward.length()) / (camera.H_Angle * M_PI_180);
            y = atan2(b * upLength, hypot(a * rightLength, c * forward.length())) / (camera.V_Angle * M_PI_180);
            break;
        case PANORAMIC_CAMERA:
            x = atan2(a, c) / M_PI;
            y = atan2(b, hypot(a, c)) / M_PI;
            break;
        case FISHEYE_CAMERA:
        case OMNIMAX_CAMERA:
        {
            const DBL radial = hypot(a, b);
            const DBL theta = atan2(radial, c);
            DBL radius;
            if (camera.Type == FISHEYE_CAMERA)
            {
                if (angle <= 0.0) return false;
                radius = 2.0 * theta / angle;
                x = (radial > EPSILON) ? radius * a / radial / (2.0 * rightLength) : 0.0;
                y = (radial > EPSILON) ? radius * b / radial / (2.0 * upLength) : 0.0;
            }
            else
            {
                if (b < -c) return false;
                DBL lower = 0.0, upper = 2.0;
                for (unsigned int i = 0; i < 32; ++i)
                {
                    radius = (lower + upper) * 0.5;
                    const DBL t = radius * radius;
                    if (radius * (1.411269 - 0.09439 * t + 0.25674 * t * t) < theta)
                        lower = radius;
                    else
                        upper = radius;
                }
                radius = (lower + upper) * 0.5;
                x = (radial > EPSILON) ? radius * a / radial : 0.0;
                y = (radial > EPSILON) ? radius * b / radial : 0.0;
                const DBL aspect = rightLength / upLength;
                if (aspect > 1.0)
                {
                    const DBL scale = std::min(aspect, 1.283458);
                    if (aspect > 1.283458) x *= 1.283458 / aspect;
                    y = (y - 1.0) * scale + 1.0;
                }
                else y *= aspect;
                x *= 0.5;
                y *= 0.5;
            }
            break;
        }
        case ULTRA_WIDE_ANGLE_CAMERA:
        {
            if ((angle <= 0.0) || (c <= EPSILON)) return false;
            const DBL u = a / c, v = b / c;
            DBL horizontal = atan(u), vertical = atan(v);
            for (unsigned int i = 0; i < 24; ++i)
            {
                horizontal = atan(u * cos(vertical));
                vertical = atan(v * cos(horizontal));
            }
            x = horizontal / angle;
            y = vertical / (angle * upLength / rightLength);
            break;
        }
        default: return false;
    }
    return std::isfinite(x) && std::isfinite(y);
}

bool ProjectBounds(const Camera& camera, const Vector3d& low, const Vector3d& high, BoundsProjection& projection)
{
    if (!FiniteBounds(low, high))
        return false;
    projection = { HUGE_VAL, -HUGE_VAL, HUGE_VAL, -HUGE_VAL };
    // Sweep the transformed AABB's surface; curved projections may have extrema between corners.
    for (unsigned int i = 0; i <= 4; ++i)
        for (unsigned int j = 0; j <= 4; ++j)
            for (unsigned int k = 0; k <= 4; ++k)
            {
                if ((i != 0) && (i != 4) && (j != 0) && (j != 4) && (k != 0) && (k != 4)) continue;
                const Vector3d point(low[X] + (high[X] - low[X]) * i / 4.0,
                                     low[Y] + (high[Y] - low[Y]) * j / 4.0,
                                     low[Z] + (high[Z] - low[Z]) * k / 4.0);
                DBL x, y;
                if (!ProjectPoint(camera, point, x, y)) return false;
                projection.left = std::min(projection.left, x);
                projection.right = std::max(projection.right, x);
                projection.bottom = std::min(projection.bottom, y);
                projection.top = std::max(projection.top, y);
            }
    return true;
}

bool Camera::Frame(const Vector3d& low, const Vector3d& high)
{
    if (!FiniteBounds(low, high) || (Direction.lengthSqr() <= 0.0)) return false;
    const Vector3d center = (low + high) * 0.5;
    Vector3d forward = (center - Location).lengthSqr() > EPSILON ? (center - Location).normalized() : Direction.normalized();
    const DBL extent = (high - low).length();
    const DBL directionLength = Direction.length(), rightLength = Right.length(), upLength = Up.length();
    const DBL handedness = dot(cross(Up, Direction), Right);
    Vector3d right = cross(Sky, forward);
    if (right.lengthSqr() < EPSILON) right = Right;
    if (right.lengthSqr() < EPSILON) return false;
    right.normalize();
    Up = cross(forward, right).normalized() * upLength;
    Right = right * ((handedness > 0.0) ? rightLength : -rightLength);
    Direction = forward * directionLength;
    if ((Type == ORTHOGRAPHIC_CAMERA) || (Type == CYL_3_CAMERA) || (Type == CYL_4_CAMERA))
    {
        Location = center - forward * extent * 2.0;
        BoundsProjection projected;
        if (!ProjectBounds(*this, low, high, projected)) return false;
        if ((Type == ORTHOGRAPHIC_CAMERA) || (Type == CYL_4_CAMERA))
            Right *= std::max(fabs(projected.left), fabs(projected.right)) / 0.45;
        if ((Type == ORTHOGRAPHIC_CAMERA) || (Type == CYL_3_CAMERA))
            Up *= std::max(fabs(projected.bottom), fabs(projected.top)) / 0.45;
    }
    auto fits = [&](DBL distance) {
        Location = center - forward * distance;
        BoundsProjection projected;
        if (!ProjectBounds(*this, low, high, projected)) return false;
        if (Type == FISHEYE_CAMERA)
            if (hypot(std::max(fabs(projected.left), fabs(projected.right)) * Right.length(),
                      std::max(fabs(projected.bottom), fabs(projected.top)) * Up.length()) > 0.45) return false;
        if (Type == OMNIMAX_CAMERA)
        {
            const DBL aspect = Right.length() / Up.length();
            DBL left = projected.left * 2.0, right = projected.right * 2.0;
            DBL bottom = projected.bottom * 2.0, top = projected.top * 2.0;
            if (aspect > 1.0)
            {
                if (aspect > 1.283458) { left *= aspect / 1.283458; right *= aspect / 1.283458; }
                const DBL scale = std::min(aspect, 1.283458);
                bottom = (bottom - 1.0) / scale + 1.0;
                top = (top - 1.0) / scale + 1.0;
            }
            else { bottom /= aspect; top /= aspect; }
            if (hypot(std::max(fabs(left), fabs(right)), std::max(fabs(bottom), fabs(top))) > 0.95) return false;
        }
        return (projected.left >= -0.45) && (projected.right <= 0.45) &&
               (projected.bottom >= -0.45) && (projected.top <= 0.45);
    };
    DBL lower = 0.0, upper = extent;
    unsigned int expansions = 0;
    while (!fits(upper) && (++expansions < 64)) upper *= 2.0;
    if (expansions == 64) return false;
    for (unsigned int i = 0; i < 40; ++i)
    {
        const DBL distance = (lower + upper) * 0.5;
        if (fits(distance)) upper = distance;
        else lower = distance;
    }
    Location = center - forward * upper;
    Look_At = center;
    return true;
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
           (frameFilter == other.frameFilter) && (!frameBounds || (SameVector(frameLow, other.frameLow) && SameVector(frameHigh, other.frameHigh))) &&
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
