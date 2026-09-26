//******************************************************************************
///
/// @file core/shape/mesh.cpp
///
/// Implementation of the mesh geometric primitive.
///
/// @author Dieter Bayer
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

/****************************************************************************
*
*  Explanation:
*
*    -
*
*  Syntax:
*
*    mesh
*    {
*      triangle { <CORNER1>, <CORNER2>, <CORNER3>, texture { NAME } }
*      smooth_triangle { <CORNER1>, <NORMAL1>, <CORNER2>, <NORMAL2>, <CORNER3>, <NORMAL3>, texture { NAME } }
*      ...
*      [ hierarchy FLAG ]
*    }
*
*  ---
*
*  Feb 1995 : Creation. [DB]
*
*****************************************************************************/

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/shape/mesh.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <limits>

// POV-Ray header files (base module)
#include "base/pov_err.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/material/texture.h"
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/triangle.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

/*****************************************************************************
* Local preprocessor defines
******************************************************************************/

const DBL DEPTH_TOLERANCE = 1e-6;

#define max3_coordinate(x,y,z) ((x > y) ? ((x > z) ? X : Z) : ((y > z) ? Y : Z))

const int HASH_SIZE = 1000;

const int INITIAL_NUMBER_OF_ENTRIES = 256;



/*****************************************************************************
* Local variables
******************************************************************************/

HASH_TABLE **Mesh::Vertex_Hash_Table;
HASH_TABLE **Mesh::Normal_Hash_Table;
UV_HASH_TABLE **Mesh::UV_Hash_Table;

/*****************************************************************************
*
* FUNCTION
*
*   All_Mesh_Intersections
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
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    Thread->Stats()[Ray_Mesh_Tests]++;

    if (Intersect(ray, Depth_Stack, Thread))
    {
        Thread->Stats()[Ray_Mesh_Tests_Succeeded]++;
        return(true);
    }

    return(false);
}



/*****************************************************************************
*
* FUNCTION
*
*   Intersect_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::Intersect(const BasicRay& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    MeshIndex i;
    bool found;
    DBL len, t;
    BasicRay New_Ray;

    /* Transform the ray into mesh space. */

    if (Trans != nullptr)
    {
        MInvTransRay(New_Ray, ray, Trans);

        len = New_Ray.Direction.length();
        New_Ray.Direction /= len;
    }
    else
    {
        New_Ray = ray;

        len = 1.0;
    }

    found = false;

    if (Data->FlatTree == nullptr)
    {
        /* There's no bounding hierarchy so just step through all elements. */

        for (i = 0; i < Data->Number_Of_Triangles; i++)
        {
            if (intersect_mesh_triangle(New_Ray, &Data->Triangles[i], &t))
            {
                if (test_hit(&Data->Triangles[i], ray, t, len, Depth_Stack, Thread))
                {
                    found = true;
                }
            }
        }
    }
    else
    {
        /* Use the mesh's bounding hierarchy. */

        return(intersect_bbox_tree(New_Ray, ray, len, Depth_Stack, Thread));
    }

    return(found);
}



/*****************************************************************************
*
* FUNCTION
*
*   Inside_Mesh
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Nathan Kopp & Dieter Bayer (adapted from Intersect_Mesh by Dieter Bayer)
*
* DESCRIPTION
*
*   Shoot a ray out from this point, if the ray hits an odd number of
*     triangles, it is inside the object.  If it hits an even number
*     than it is outside the object.
*   The triangle mesh should be closed, otherwise results are unpredictable.
*
* CHANGES
*
*   October, 1998 : Creation.
*
******************************************************************************/

bool Mesh::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    bool inside;
    MeshIndex i;
    unsigned int found;
    DBL t;
    BasicRay ray;

    if (has_inside_vector==false)
        return false;

    ray.Direction = Data->Inside_Vect;
    ray.Origin = IPoint;

    /* Transform the ray into mesh space. */
    if (Trans != nullptr)
    {
        MInvTransRay(ray, ray, Trans);

        ray.Direction.normalize();
    }

    found = 0;

    if (Data->FlatTree == nullptr)
    {
        /* just step through all elements. */
        for (i = 0; i < Data->Number_Of_Triangles; i++)
        {
            if (intersect_mesh_triangle(ray, &Data->Triangles[i], &t))
            {
                /* actually, this should push onto a local depth stack and
                   make sure that we don't have the same intersection point from
                   two (or three) different triangles!!!!! */
                found++;
            }
        }
        /* odd number = inside, even number = outside */
        inside = ((found & 1) != 0);
    }
    else
    {
        /* Use the mesh's bounding hierarchy. */
        inside = inside_bbox_tree(ray, Thread->Stats());
    }

    if (Test_Flag(this, INVERTED_FLAG))
    {
        inside = !inside;
    }
    return (inside);
}




/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Normal
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
*   Return the normalized normal in the given point.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Normal(Vector3d& Result, Intersection *Inter, TraceThreadData *Thread) const
{
    Vector3d IPoint;
    const MESH_TRIANGLE *Triangle;

    Triangle = reinterpret_cast<const MESH_TRIANGLE *>(Inter->Pointer);

    if (Triangle->Smooth())
    {
        if (Trans != nullptr)
        {
            MInvTransPoint(IPoint, Inter->IPoint, Trans);
        }
        else
        {
            IPoint = Inter->IPoint;
        }

        Smooth_Mesh_Normal(Result, Triangle, IPoint);

        if (Trans != nullptr)
        {
            MTransNormal(Result, Result, Trans);
        }

        Result.normalize();
    }
    else
    {
        Result = Face_Normal(Triangle);

        if (Trans != nullptr)
        {
            MTransNormal(Result, Result, Trans);

            Result.normalize();
        }
    }
}



// A triangle's smoothing frame: Perp scaled to measure 0..1 from edge P2-P3 towards P1, and the dominant axis of that edge.
static void smooth_frame(const Vector3d& P1, const Vector3d& P2, const Vector3d& P3, Vector3d& Perp, int& vAxis)
{
    const Vector3d P3MinusP2 = P3 - P2;
    vAxis = max3_coordinate(fabs(P3MinusP2[X]), fabs(P3MinusP2[Y]), fabs(P3MinusP2[Z]));

    Vector3d VTemp1 = (P2 - P3).normalized();
    const Vector3d VTemp2 = P1 - P3;
    VTemp1 *= dot(VTemp2, VTemp1);
    Perp = (VTemp1 - VTemp2).normalized();
    Perp /= -dot(VTemp2, Perp);
}

/*****************************************************************************
*
* FUNCTION
*
*   Smooth_Mesh_Normal
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
*   Remove the un-normalized normal of a smoothed triangle.
*
* CHANGES
*
*   Feb 1995 : Creation. (Derived from TRIANGLE.C)
*
******************************************************************************/

void Mesh::Smooth_Mesh_Normal(Vector3d& Result, const MESH_TRIANGLE *Triangle, const Vector3d& IPoint) const
{
    int axis;
    DBL u, v;
    Vector3d PIMinusP1, P1, P2, P3, N1, N2, N3, Perp;

    get_triangle_vertices(Triangle, P1, P2, P3);
    get_triangle_normals(Triangle, N1, N2, N3);
    smooth_frame(P1, P2, P3, Perp, axis);

    PIMinusP1 = IPoint - P1;

    u = dot(PIMinusP1, Perp);

    if (u < EPSILON)
    {
        Result = N1;
    }
    else
    {
        v = (PIMinusP1[axis] / u + P1[axis] - P2[axis]) / (P3[axis] - P2[axis]);

        Result = N1 + u * (N2 - N1 + v * (N3 - N2));
    }
}

Vector3d Mesh::Face_Normal(const MESH_TRIANGLE *Triangle) const
{
    Vector3d P1, P2, P3;

    get_triangle_vertices(Triangle, P1, P2, P3);
    const Vector3d N = cross(P3 - P1, P2 - P1);
    const DBL len = N.length();
    if (len == 0.0)
        return N;
    return N * ((Triangle->Flipped() ? -1.0 : 1.0) / len);
}



/*****************************************************************************
*
* FUNCTION
*
*   Translate_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Translate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Rotate_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Rotate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Scale_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Scale(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Transfrom_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Transform(const TRANSFORM *tr)
{
    MeshIndex i;

    if (Trans == nullptr)
    {
        Trans = Create_Transform();
    }

    Recompute_BBox(&BBox, tr);

    Compose_Transforms(Trans, tr);

    /* NK 1998 added if */
    if (!Test_Flag(this, UV_FLAG))
        for (i=0; i<Number_Of_Textures; i++)
            Transform_Textures(Textures[i], tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Create_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

Mesh::Mesh() : ObjectBase(MESH_OBJECT)
{
    Set_Flag(this, HIERARCHY_FLAG);

    Trans = nullptr;

    Data = nullptr;

    has_inside_vector=false;

    Number_Of_Textures=0; /* [LSK] these were uninitialized */
    Textures = nullptr;
}



/*****************************************************************************
*
* FUNCTION
*
*   Copy_Mesh
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
*   Copy a mesh.
*
*   NOTE: The components are not copied, only the number of references is
*         counted, so that Destroy_Mesh() knows if they can be destroyed.
*
*   -
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

ObjectPtr Mesh::Copy()
{
    Mesh *New = new Mesh();
    MeshIndex i;

    Destroy_Transform(New->Trans);
    *New = *this;
    New->Trans = Copy_Transform(Trans);

    New->Data = Data;
    New->Data->References++;

    /* NK 1999 copy textures */
    if (Textures != nullptr)
    {
        New->Textures = reinterpret_cast<TEXTURE **>(POV_MALLOC(Number_Of_Textures*sizeof(TEXTURE *), "triangle mesh data"));
        for (i = 0; i < Number_Of_Textures; i++)
            New->Textures[i] = Copy_Textures(Textures[i]);
    }

    return(New);
}



/*****************************************************************************
*
* FUNCTION
*
*   Destroy_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

Mesh::~Mesh()
{
    MeshIndex i;

    /* NK 1999 move texture outside of data block */
    if (Textures != nullptr)
    {
        for (i = 0; i < Number_Of_Textures; i++)
        {
            Destroy_Textures(Textures[i]);
        }

        POV_FREE(Textures);
    }

    if (--(Data->References) == 0)
    {
        delete Data->FlatTree;

        if (Data->Normals != nullptr)
        {
            POV_FREE(Data->Normals);
        }

        /* NK 1998 */
        if (Data->UVCoords != nullptr)
        {
            POV_FREE(Data->UVCoords);
        }
        /* NK ---- */

        if (Data->Vertices != nullptr)
        {
            POV_FREE(Data->Vertices);
        }

        if (Data->Triangles != nullptr)
        {
            POV_FREE(Data->Triangles);
        }

        delete Data;
    }
}



/*****************************************************************************
*
* FUNCTION
*
*   Compute_Mesh_BBox
*
* INPUT
*
*   Mesh - Mesh
*
* OUTPUT
*
*   Mesh
*
* RETURNS
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Calculate the bounding box of a triangle.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Compute_BBox()
{
    MeshIndex i;
    Vector3d P1, P2, P3;
    Vector3d mins, maxs;

    mins = Vector3d(BOUND_HUGE);
    maxs = Vector3d(-BOUND_HUGE);

    for (i = 0; i < Data->Number_Of_Triangles; i++)
    {
        get_triangle_vertices(&Data->Triangles[i], P1, P2, P3);

        mins = min(mins, P1, P2, P3);
        maxs = max(maxs, P1, P2, P3);
    }

    Make_BBox_from_min_max(BBox, mins, maxs);
}



/*****************************************************************************
*
* FUNCTION
*
*   Compute_Mesh
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
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::Compute_Mesh_Triangle(MESH_TRIANGLE *Triangle, MeshIndex Index, bool Smooth, const Vector3d& P1, const Vector3d& P2, const Vector3d& P3)
{
    bool swap;
    Vector3d S_Normal;
    const size_t tri = size_t(Index);

    S_Normal = cross(P3 - P1, P2 - P1);

    /* Set up a flag so we can ignore degenerate triangles */

    if (S_Normal.length() == 0.0)
    {
        return(false);
    }

    /* Find triangle's dominant axis. */

    Triangle->SetDominantAxis(max3_coordinate(fabs(S_Normal[X]), fabs(S_Normal[Y]), fabs(S_Normal[Z])));

    swap = false;

    switch (Triangle->Dominant_Axis())
    {
        case X:

            if ((P2[Y] - P3[Y])*(P2[Z] - P1[Z]) < (P2[Z] - P3[Z])*(P2[Y] - P1[Y]))
            {
                swap = true;
            }

            break;

        case Y:

            if ((P2[X] - P3[X])*(P2[Z] - P1[Z]) < (P2[Z] - P3[Z])*(P2[X] - P1[X]))
            {
                swap = true;
            }

            break;

        case Z:

            if ((P2[X] - P3[X])*(P2[Y] - P1[Y]) < (P2[Y] - P3[Y])*(P2[X] - P1[X]))
            {
                swap = true;
            }

            break;
    }

    if (swap)
    {
        const MeshIndex temp = Triangle->P1();
        Triangle->SetP(0, Triangle->P2());
        Triangle->SetP(1, temp);
        Triangle->SetFlipped(true);

        Data->UVInd.Swap(tri, 0, 1);

        if (Triangle->ThreeTex())
        {
            const MeshIndex t1 = Data->TextureInd.Get(*Triangle, tri, 0), t2 = Data->Texture23Ind.Get(*Triangle, tri, 0);
            Data->TextureInd.Set(tri, 0, t2);
            Data->Texture23Ind.Set(tri, 0, t1);
        }

        if (Smooth)
        {
            Data->NormalInd.Swap(tri, 0, 1);
        }
    }

    if (Smooth)
    {
        Triangle->SetSmooth(true);
    }

    return(true);
}

static bool Smooth_Triangle(const MESH_TRIANGLE& t) { return t.Smooth(); }
static bool Three_Tex_Triangle(const MESH_TRIANGLE& t) { return t.ThreeTex(); }
static bool Any_Triangle(const MESH_TRIANGLE&) { return true; }

void Mesh::Finish_Mesh_Data()
{
    const size_t n = size_t(Data->Number_Of_Triangles);
    Data->NormalInd.Finish(Data->Triangles, n, true, Smooth_Triangle);
    Data->UVInd.Finish(Data->Triangles, n, true, Any_Triangle);
    Data->TextureInd.Finish(Data->Triangles, n, false, Any_Triangle);
    Data->Texture23Ind.Finish(Data->Triangles, n, false, Three_Tex_Triangle);
}

void MeshIndexColumn::Set(size_t tri, int k, MeshIndex v)
{
    const size_t i = tri * width + k;
    if (values.empty() && (v == fill) && !byVertex)
        return;
    POV_ASSERT(!byVertex);
    if (values.size() <= i)
        values.resize((tri + 1) * width, fill);
    values[i] = v;
}

void MeshIndexColumn::Swap(size_t tri, int j, int k)
{
    if (!values.empty())
        std::swap(values[tri * width + j], values[tri * width + k]);
}

void MeshIndexColumn::Finish(const Mesh_Triangle_Struct *triangles, size_t n, bool vertexLike, bool (*relevant)(const Mesh_Triangle_Struct&))
{
    if (values.empty())
        return;
    values.resize(n * width, fill);
    bool sameAsVertex = vertexLike, constant = true, none = true;
    MeshIndex value = fill;
    for (size_t t = 0; (t < n) && (sameAsVertex || constant); ++t)
    {
        if (!relevant(triangles[t]))
            continue;
        for (int k = 0; k < width; ++k)
        {
            const MeshIndex v = values[t * width + k];
            sameAsVertex = sameAsVertex && (v == triangles[t].P(k));
            constant = constant && (none || (v == value));
            value = v;
            none = false;
        }
    }
    if (!sameAsVertex && !constant)
    {
        values.shrink_to_fit();
        return;
    }
    byVertex = sameAsVertex && !none;
    if (!byVertex && !none)
        fill = value;
    std::vector<MeshIndex>().swap(values);
}



/*****************************************************************************
*
* FUNCTION
*
*   intersect_mesh_triangle
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
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::intersect_mesh_triangle(const BasicRay &ray, const MESH_TRIANGLE *Triangle, DBL *Depth) const
{
    DBL NormalDotDirection;
    DBL s, t;
    Vector3d P1, P2, P3, S_Normal;

    get_triangle_vertices(Triangle, P1, P2, P3);

    // Unnormalised: the depth needs no unit normal, and the grazing test scales by its length instead.
    S_Normal = cross(P3 - P1, P2 - P1);

    NormalDotDirection = dot(S_Normal, ray.Direction);

    if (!(NormalDotDirection * NormalDotDirection > EPSILON * EPSILON * S_Normal.lengthSqr()))
    {
        return(false);
    }

    *Depth = dot(S_Normal, P1 - ray.Origin) / NormalDotDirection;

    if ((*Depth < DEPTH_TOLERANCE) || (*Depth > MAX_DISTANCE))
    {
        return(false);
    }

    switch (Triangle->Dominant_Axis())
    {
        case X:

            s = ray.Origin[Y] + *Depth * ray.Direction[Y];
            t = ray.Origin[Z] + *Depth * ray.Direction[Z];

            if ((P2[Y] - s) * (P2[Z] - P1[Z]) < (P2[Z] - t) * (P2[Y] - P1[Y]))
            {
                return(false);
            }

            if ((P3[Y] - s) * (P3[Z] - P2[Z]) < (P3[Z] - t) * (P3[Y] - P2[Y]))
            {
                return(false);
            }

            if ((P1[Y] - s) * (P1[Z] - P3[Z]) < (P1[Z] - t) * (P1[Y] - P3[Y]))
            {
                return(false);
            }

            return(true);

        case Y:

            s = ray.Origin[X] + *Depth * ray.Direction[X];
            t = ray.Origin[Z] + *Depth * ray.Direction[Z];

            if ((P2[X] - s) * (P2[Z] - P1[Z]) < (P2[Z] - t) * (P2[X] - P1[X]))
            {
                return(false);
            }

            if ((P3[X] - s) * (P3[Z] - P2[Z]) < (P3[Z] - t) * (P3[X] - P2[X]))
            {
                return(false);
            }

            if ((P1[X] - s) * (P1[Z] - P3[Z]) < (P1[Z] - t) * (P1[X] - P3[X]))
            {
                return(false);
            }

            return(true);

        case Z:

            s = ray.Origin[X] + *Depth * ray.Direction[X];
            t = ray.Origin[Y] + *Depth * ray.Direction[Y];

            if ((P2[X] - s) * (P2[Y] - P1[Y]) < (P2[Y] - t) * (P2[X] - P1[X]))
            {
                return(false);
            }

            if ((P3[X] - s) * (P3[Y] - P2[Y]) < (P3[Y] - t) * (P3[X] - P2[X]))
            {
                return(false);
            }

            if ((P1[X] - s) * (P1[Y] - P3[Y]) < (P1[Y] - t) * (P1[X] - P3[X]))
            {
                return(false);
            }

            return(true);
    }

    return(false);
}



/*
 *  MeshUV - By Xander Enzmann
 *
 *  Currently unused
 *
 */

const DBL BARY_VAL1 = -0.00001;
const DBL BARY_VAL2 =  1.00001;

void Mesh::MeshUV(const Vector3d& P, const MESH_TRIANGLE *Triangle, Vector2d& Result) const
{
    DBL a, b, r;
    Vector3d Q;
    Matrix3x3 B, IB;
    Vector3d P1, P2, P3;
    Vector2d UV1, UV2, UV3;

    get_triangle_vertices(Triangle, P1, P2, P3);
    B[0] = P2 - P1;
    B[1] = P3 - P1;
    B[2] = Face_Normal(Triangle);

    if (!MInvers3(B, IB)) {
        // Failed to invert - that means this is a degenerate triangle
        Result[U] = P[X];
        Result[V] = P[Y];
        return;
    }

    Q = P - P1;
    a = dot(Q, IB[0]);
    b = dot(Q, IB[1]);

    if (a < BARY_VAL1 || b < BARY_VAL1 || a + b > BARY_VAL2)
    {
        // The use of BARY_VAL1 is an attempt to compensate for the
        //  lack of precision in the floating point numbers used in
        //  the matrices B and IB.  Since floats only have around
        //  7 digits of precision, we make sure that we allow any
        //  slop in a and b that is less than that. */
        Result[U] = P[X];
        Result[V] = P[Y];
        return;
    }

    r = 1.0f - a - b;

    get_triangle_uvcoords(Triangle, UV1, UV2, UV3);

    Result = r * UV1 + a * UV2 + b * UV3;
}


/*****************************************************************************
*
* FUNCTION
*
*   test_hit
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
*   Test if a hit is valid and push if on the intersection depth.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::test_hit(const MESH_TRIANGLE *Triangle, const BasicRay &OrigRay, DBL Depth, DBL len, IStack& Depth_Stack, TraceThreadData *Thread)
{
    Vector3d IPoint;
    DBL world_dist = Depth / len;

    IPoint = OrigRay.Evaluate(world_dist);

    if (Clip.empty() || Point_In_Clip(IPoint, Clip, Thread))
    {
        /*
        don't bother calling MeshUV because we reclaculate it later (if needed) anyway
        Vector2d uv;
        Vector3d P; // Object coordinates of intersection
        P = ray.Evaluate(Depth);

        MeshUV(P, Triangle, Mesh, uv);

        push_entry_pointer_uv(world_dist, IPoint, uv, Object, Triangle, Depth_Stack);
        */

        Depth_Stack->push(Intersection(world_dist, IPoint, this, Triangle));
        return(true);
    }

    return(false);
}



/*****************************************************************************
*
* FUNCTION
*
*   Init_Mesh_Triangle
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Init_Mesh_Triangle(MESH_TRIANGLE *Triangle)
{
    Triangle->Word[0] = Triangle->Word[1] = Triangle->Word[2] = 0;
}



/*****************************************************************************
*
* FUNCTION
*
*   get_triangle_bbox
*
* INPUT
*
*   Triangle - Pointer to triangle
*
* OUTPUT
*
*   BBox     - Bounding box
*
* RETURNS
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Calculate the bounding box of a triangle.
*
* CHANGES
*
*   Sep 1994 : Creation.
*
******************************************************************************/

void Mesh::get_triangle_bbox(const MESH_TRIANGLE *Triangle, BoundingBox *BBox) const
{
    Vector3d P1, P2, P3;
    Vector3d Min, Max;

    get_triangle_vertices(Triangle, P1, P2, P3);

    Min = min(P1, P2, P3);
    Max = max(P1, P2, P3);

    Make_BBox_from_min_max(*BBox, Min, Max);
}



/*****************************************************************************
*
* FUNCTION
*
*   Build_Mesh_BBox_Tree
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
*   Create the bounding box hierarchy.
*
* CHANGES
*
*   Feb 1995 : Creation. (Derived from the bounding slab creation code)
*
******************************************************************************/

void Mesh::Build_Mesh_BBox_Tree()
{
    if (!Test_Flag(this, HIERARCHY_FLAG))
    {
        return;
    }

    delete Data->FlatTree;
    Data->FlatTree = Build_Flat_BBox_Tree(size_t(Data->Number_Of_Triangles), [this](size_t i, BoundingBox& box) {
        get_triangle_bbox(&Data->Triangles[i], &box);
    });
}




/*****************************************************************************
*
* FUNCTION
*
*   intersect_bbox_tree
*
* INPUT
*
*   Mesh     - Mesh object
*   Ray      - Current ray
*   Orig_Ray - Original, untransformed ray
*   len      - Length of the transformed ray direction
*
* OUTPUT
*
*   Depth_Stack - Stack of intersections
*
* RETURNS
*
*   bool - true if an intersection was found
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Intersect a ray with the bounding box tree of a mesh.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::intersect_bbox_tree(const BasicRay &ray, const BasicRay &Orig_Ray, DBL len, IStack& Depth_Stack, TraceThreadData *Thread)
{
    bool found = false;
    DBL Best = BOUND_HUGE;

    Traverse_Flat_BBox_Tree(*Data->FlatTree, ray, Best, !has_inside_vector, Thread->Stats(), [&](std::int32_t leaf) {
        const MESH_TRIANGLE *triangle = &Data->Triangles[leaf];
        DBL hit;
        if (intersect_mesh_triangle(ray, triangle, &hit) && test_hit(triangle, Orig_Ray, hit, len, Depth_Stack, Thread))
        {
            found = true;
            Best = std::min(Best, hit);
        }
        return false;
    });
    return found;
}




/*****************************************************************************
*
* FUNCTION
*
*   mesh_hash
*
* INPUT
*
*   aPoint - Normal/Vertex to store
*
* OUTPUT
*
*   Hash_Table - Normal/Vertex hash table
*   Number     - Number of normals/vertices
*   Max        - Max. number of normals/vertices
*   Elements   - List of normals/vertices
*
* RETURNS
*
*   MeshIndex - Index of normal/vertex into the normals/vertices list
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Try to locate a triangle normal/vertex in the normal/vertex list.
*   If the vertex is not found its stored in the normal/vertex list.
*
* CHANGES
*
*   Feb 1995 : Creation. (With help from Steve Anger's RAW2POV code)
*
******************************************************************************/

MeshIndex Mesh::mesh_hash(HASH_TABLE **Hash_Table, MeshIndex *Number, MeshIndex  *Max, MeshVector **Elements, const Vector3d& aPoint)
{
    int hash;
    MeshVector D, P;
    HASH_TABLE *p;

    P = MeshVector(aPoint);

    /* Get hash value. */

    // TODO - This is inefficient for very small meshes. Maybe scale by 2^N (or 2^-N) into the range 1.0..2.0 first.
    hash = (unsigned int)((int)(326.0*P[X])^(int)(694.7*P[Y])^(int)(1423.6*P[Z])) % HASH_SIZE;

    /* Try to find normal/vertex. */

    for (p = Hash_Table[hash]; p != nullptr; p = p->Next)
    {
        D = p->P - P;

        if ((fabs(D[X]) < EPSILON) && (fabs(D[Y]) < EPSILON) && (fabs(D[Z]) < EPSILON))
        {
            break;
        }
    }

    if ((p != nullptr) && (p->Index >= 0))
    {
        return(p->Index);
    }

    /* Add new normal/vertex to the list and hash table. */

    if ((*Number) >= (*Max))
    {
        if ((*Max) >= std::numeric_limits<MeshIndex>::max()/2)
        {
            throw POV_EXCEPTION_STRING("Too many normals/vertices in mesh.");
        }

        (*Max) *= 2;

        (*Elements) = reinterpret_cast<MeshVector *>(POV_REALLOC((*Elements), (*Max)*sizeof(MeshVector), "mesh data"));
    }

    (*Elements)[*Number] = P;

    p = reinterpret_cast<HASH_TABLE *>(POV_MALLOC(sizeof(HASH_TABLE), "mesh data"));

    p->P = P;

    p->Index = *Number;

    p->Next = Hash_Table[hash];

    Hash_Table[hash] = p;

    return((*Number)++);
}



/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Hash_Vertex
*
* INPUT
*
*   Vertex - Vertex to store
*
* OUTPUT
*
*   Number_Of_Vertices - Number of vertices
*   Max_Vertices       - Max. number of vertices
*   Vertices           - List of vertices
*
* RETURNS
*
*   MeshIndex - Index of vertex into the vertices list
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Try to locate a triangle vertex in the vertex list.
*   If the vertex is not found its stored in the vertex list.
*
* CHANGES
*
*   Feb 1995 : Creation. (With help from Steve Anger's RAW2POV code)
*
******************************************************************************/

MeshIndex Mesh::Mesh_Hash_Vertex(MeshIndex *Number_Of_Vertices, MeshIndex *Max_Vertices, MeshVector **Vertices, const Vector3d& Vertex)
{
    return(mesh_hash(Vertex_Hash_Table, Number_Of_Vertices, Max_Vertices, Vertices, Vertex));
}



/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Hash_Normal
*
* INPUT
*
*   Normal - Normal to store
*
* OUTPUT
*
*   Number_Of_Normals - Number of normals
*   Max_Normals       - Max. number of normals
*   Normals           - List of normals
*
* RETURNS
*
*   MeshIndex - Index of normal into the normals list
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Try to locate a triangle normal in the normal list.
*   If the normal is not found its stored in the normal list.
*
* CHANGES
*
*   Feb 1995 : Creation. (With help from Steve Anger's RAW2POV code)
*
******************************************************************************/

MeshIndex Mesh::Mesh_Hash_Normal(MeshIndex *Number_Of_Normals, MeshIndex *Max_Normals, MeshVector **Normals, const Vector3d& S_Normal)
{
    return(mesh_hash(Normal_Hash_Table, Number_Of_Normals, Max_Normals, Normals, S_Normal));
}



/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Hash_Texture
*
* INPUT
*
*   Texture - Texture to store
*
* OUTPUT
*
*   Number_Of_Textures - Number of textures
*   Max_Textures       - Max. number of textures
*   Textures           - List of textures
*
* RETURNS
*
*   MeshIndex - Index of texture into the texture list
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Try to locate a texture in the texture list.
*   If the texture is not found its stored in the texture list.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

MeshIndex Mesh::Mesh_Hash_Texture(MeshIndex *Number_Of_Textures, MeshIndex *Max_Textures, TEXTURE ***Textures, TEXTURE *Texture)
{
    MeshIndex i;

    if (Texture == nullptr)
    {
        return(-1);
    }

    /* Just do a linear search. */

    for (i = 0; i < *Number_Of_Textures; i++)
    {
        if ((*Textures)[i] == Texture)
        {
            break;
        }
    }

    if (i == *Number_Of_Textures)
    {
        if ((*Number_Of_Textures) >= (*Max_Textures))
        {
            if ((*Max_Textures) >= std::numeric_limits<MeshIndex>::max()/2)
            {
                throw POV_EXCEPTION_STRING("Too many textures in mesh.");
            }

            (*Max_Textures) *= 2;

            (*Textures) = reinterpret_cast<TEXTURE **>(POV_REALLOC((*Textures), (*Max_Textures)*sizeof(TEXTURE *), "mesh data"));
        }

        (*Textures)[(*Number_Of_Textures)++] = Copy_Texture_Pointer(Texture);
    }

    return(i);
}

/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Hash_UV
*
* INPUT
*
*   aPoint - UV vector to store
*
* OUTPUT
*
*   Hash_Table - UV vector hash table
*   Number     - Number of UV vectors
*   Max        - Max. number of UV vectors
*   Elements   - List of UV vectors
*
* RETURNS
*
*   MeshIndex - Index of UV vector into the UV vector list
*
* AUTHOR
*
*   Dieter Bayer / Nathan Kopp
*
* DESCRIPTION
*
*   adapted from mesh_hash
*
* CHANGES
*
*
******************************************************************************/

MeshIndex Mesh::Mesh_Hash_UV(MeshIndex *Number, MeshIndex *Max, MeshUVVector **Elements, const Vector2d& aPoint)
{
    int hash;
    MeshUVVector D, P;
    UV_HASH_TABLE *p;

    P = MeshUVVector(aPoint);

    /* Get hash value. */

    // TODO - This is inefficient for very small meshes. Maybe scale by 2^N (or 2^-N) into the range 1.0..2.0 first.
    hash = (unsigned int)((int)(326.0*P[U])^(int)(694.7*P[V])) % HASH_SIZE;

    /* Try to find normal/vertex. */

    for (p = UV_Hash_Table[hash]; p != nullptr; p = p->Next)
    {
        /* VSub(D, p->P, P); */
        D = p->P - P;

        if ((fabs(D[U]) < EPSILON) && (fabs(D[V]) < EPSILON))
        {
            break;
        }
    }

    if ((p != nullptr) && (p->Index >= 0))
    {
        return(p->Index);
    }

    /* Add new normal/vertex to the list and hash table. */

    if ((*Number) >= (*Max))
    {
        if ((*Max) >= std::numeric_limits<MeshIndex>::max()/2)
        {
            throw POV_EXCEPTION_STRING("Too many normals/vertices in mesh.");
        }

        (*Max) *= 2;

        (*Elements) = reinterpret_cast<MeshUVVector *>(POV_REALLOC((*Elements), (*Max)*sizeof(MeshUVVector), "mesh data"));
    }

    (*Elements)[*Number] = P;

    p = reinterpret_cast<UV_HASH_TABLE *>(POV_MALLOC(sizeof(UV_HASH_TABLE), "mesh data"));

    p->P = P;

    p->Index = *Number;

    p->Next = UV_Hash_Table[hash];

    UV_Hash_Table[hash] = p;

    return((*Number)++);
}



/*****************************************************************************
*
* FUNCTION
*
*   Create_Mesh_Hash_Tables
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Create_Mesh_Hash_Tables()
{
    MeshIndex i;

    Vertex_Hash_Table = reinterpret_cast<HASH_TABLE **>(POV_MALLOC(HASH_SIZE*sizeof(HASH_TABLE *), "mesh hash table"));

    for (i = 0; i < HASH_SIZE; i++)
    {
        Vertex_Hash_Table[i] = nullptr;
    }

    Normal_Hash_Table = reinterpret_cast<HASH_TABLE **>(POV_MALLOC(HASH_SIZE*sizeof(HASH_TABLE *), "mesh hash table"));

    for (i = 0; i < HASH_SIZE; i++)
    {
        Normal_Hash_Table[i] = nullptr;
    }

    /* NK 1998 */
    UV_Hash_Table = reinterpret_cast<UV_HASH_TABLE **>(POV_MALLOC(HASH_SIZE*sizeof(UV_HASH_TABLE *), "mesh hash table"));

    for (i = 0; i < HASH_SIZE; i++)
    {
        UV_Hash_Table[i] = nullptr;
    }
    /* NK ---- */
}



/*****************************************************************************
*
* FUNCTION
*
*   Destroy_Mesh_Hash_Tables
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::Destroy_Mesh_Hash_Tables()
{
    MeshIndex i;
    HASH_TABLE *Temp;
    /* NK 1998 */
    UV_HASH_TABLE *UVTemp;
    /* NK ---- */

    for (i = 0; i < HASH_SIZE; i++)
    {
        while (Vertex_Hash_Table[i] != nullptr)
        {
            Temp = Vertex_Hash_Table[i];

            Vertex_Hash_Table[i] = Temp->Next;

            POV_FREE(Temp);
        }
    }

    POV_FREE(Vertex_Hash_Table);

    for (i = 0; i < HASH_SIZE; i++)
    {
        while (Normal_Hash_Table[i] != nullptr)
        {
            Temp = Normal_Hash_Table[i];

            Normal_Hash_Table[i] = Temp->Next;

            POV_FREE(Temp);
        }
    }

    POV_FREE(Normal_Hash_Table);

    /* NK 1998 */
    for (i = 0; i < HASH_SIZE; i++)
    {
        while (UV_Hash_Table[i] != nullptr)
        {
            UVTemp = UV_Hash_Table[i];

            UV_Hash_Table[i] = UVTemp->Next;

            POV_FREE(UVTemp);
        }
    }

    POV_FREE(UV_Hash_Table);
    /* NK ---- */
}



/*****************************************************************************
*
* FUNCTION
*
*   get_triangle_vertices
*
* INPUT
*
*   Mesh     - Mesh object
*   Triangle - Triangle
*
* OUTPUT
*
* RETURNS
*
*   P1, P2, P3 - Vertices of the triangle
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::get_triangle_vertices(const MESH_TRIANGLE *Triangle, Vector3d& P1, Vector3d& P2, Vector3d& P3) const
{
    P1 = Vector3d(Data->Vertices[Triangle->P1()]);
    P2 = Vector3d(Data->Vertices[Triangle->P2()]);
    P3 = Vector3d(Data->Vertices[Triangle->P3()]);
}



/*****************************************************************************
*
* FUNCTION
*
*   get_triangle_normals
*
* INPUT
*
*   Mesh     - Mesh object
*   Triangle - Triangle
*
* OUTPUT
*
* RETURNS
*
*   N1, N2, N3 - Normals of the triangle
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
*   Feb 1995 : Creation.
*
******************************************************************************/

void Mesh::get_triangle_normals(const MESH_TRIANGLE *Triangle, Vector3d& N1, Vector3d& N2, Vector3d& N3) const
{
    const size_t tri = size_t(Triangle - Data->Triangles);
    N1 = Vector3d(Data->Normals[Data->NormalInd.Get(*Triangle, tri, 0)]);
    N2 = Vector3d(Data->Normals[Data->NormalInd.Get(*Triangle, tri, 1)]);
    N3 = Vector3d(Data->Normals[Data->NormalInd.Get(*Triangle, tri, 2)]);
}


/*****************************************************************************
*
* FUNCTION
*
*   get_triangle_uvcoords
*
* INPUT
*
*   Mesh     - Mesh object
*   Triangle - Triangle
*
* OUTPUT
*
* RETURNS
*
*   UV1, UV2, UV3 - UV coordinates of the triangle's corners
*
* AUTHOR
*
*   Nathan Kopp
*
* DESCRIPTION
*
*   adapted from get_triangle_normals
*
* CHANGES
*
******************************************************************************/

void Mesh::get_triangle_uvcoords(const MESH_TRIANGLE *Triangle, Vector2d& UV1, Vector2d& UV2, Vector2d& UV3) const
{
    UV1 = Vector2d(Data->UVCoords[UV_Index(Triangle, 0)]);
    UV2 = Vector2d(Data->UVCoords[UV_Index(Triangle, 1)]);
    UV3 = Vector2d(Data->UVCoords[UV_Index(Triangle, 2)]);
}


/*****************************************************************************
*
* FUNCTION
*
*   Mesh_Degenerate
*
* INPUT
*
*   P1, P2, P3 - Triangle's vertices
*
* OUTPUT
*
* RETURNS
*
*   bool - true if degenerate
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Test if a triangle is degenerate.
*
* CHANGES
*
*   Feb 1995 : Creation.
*
******************************************************************************/

bool Mesh::Degenerate(const Vector3d& P1, const Vector3d& P2, const Vector3d& P3)
{
    Vector3d V1, V2, Temp;
    DBL Length;

    V1 = P1 - P2;
    V2 = P3 - P2;

    Temp = cross(V1, V2);

    Length = Temp.length();

    return(Length == 0.0);
}


/*****************************************************************************
*
* FUNCTION
*
*   Test_Mesh_Opacity
*
* INPUT
*
*   Mesh - Pointer to mesh structure
*
* OUTPUT
*
*   Mesh
*
* RETURNS
*
* AUTHOR
*
*   Dieter Bayer
*
* DESCRIPTION
*
*   Set the opacity flag of the mesh according to the opacity
*   of the mesh's texture(s).
*
* CHANGES
*
*   Apr 1996 : Creation.
*
******************************************************************************/

bool Mesh::IsOpaque() const
{
    if (Test_Flag(this, MULTITEXTURE_FLAG))
    {
        for (MeshIndex i = 0; i < Number_Of_Textures; i++)
        {
            // If component's texture isn't opaque the mesh is neither.
            if ((Textures[i] != nullptr) && !Test_Opacity(Textures[i]))
                return false;
        }
    }

    // Otherwise it's a question of whether the common texture is opaque or not.
    // TODO FIXME - other objects report as non-opaque if Texture == nullptr.
    // TODO FIXME - other objects report as non-opaque if Interior_Texture present and non-opaque.
    // What we probably really want here is `return ObjectBase::IsOpaque()`.
    return (Texture == nullptr) || Test_Opacity(Texture);
}



/*****************************************************************************
*
* FUNCTION
*
*   Mesh_UVCoord
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Nathan Kopp
*
* DESCRIPTION
*
*   computes the UV coordinates of the intersection for a mesh
*
******************************************************************************/

void Mesh::UVCoord(Vector2d& Result, const Intersection *Inter) const
{
    DBL w1, w2, w3, t1, t2;
    Vector3d vA, vB;
    Vector3d Side1, Side2;
    const MESH_TRIANGLE *Triangle;
    Vector3d P;

    if (Trans != nullptr)
        MInvTransPoint(P, Inter->IPoint, Trans);
    else
        P = Inter->IPoint;

    Triangle = reinterpret_cast<const MESH_TRIANGLE *>(Inter->Pointer);

    /* ---------------- this is for P1 ---------------- */
    /* Side1 is opposite side, Side2 is an adjacent side (vector pointing away) */
    Side1 = Vector3d(Data->Vertices[Triangle->P3()] - Data->Vertices[Triangle->P2()]);
    Side2 = Vector3d(Data->Vertices[Triangle->P3()] - Data->Vertices[Triangle->P1()]);

    /* find A */
    /* A is a vector from this vertex to the intersection point */
    vA = P - Vector3d(Data->Vertices[Triangle->P1()]);

    /* find B */
    /* B is a vector from this intersection to the opposite side (Side1) */
    /*    which is the exact length to get to the side                   */
    /* to do this we split Side2 into two components, but only keep the  */
    /*    one that's perp to Side2                                       */
    t1 = dot(Side2, Side1);
    t2 = dot(Side1, Side1);
    vB = Side1 * (t1/t2) - Side2;

    /* finding the weight is the scale part of a projection of A onto B */
    t1 = dot(vA, vB);
    t2 = dot(vB, vB);
    /* w1 = 1-fabs(t1/t2); */
    w1 = 1+t1/t2;

    /* ---------------- this is for P2 ---------------- */
    Side1 = Vector3d(Data->Vertices[Triangle->P3()] - Data->Vertices[Triangle->P1()]);
    Side2 = Vector3d(Data->Vertices[Triangle->P3()] - Data->Vertices[Triangle->P2()]);

    /* find A */
    vA = P - Vector3d(Data->Vertices[Triangle->P2()]);

    /* find B */
    t1 = dot(Side2, Side1);
    t2 = dot(Side1, Side1);
    vB = Side1 * (t1/t2) - Side2;

    t1 = dot(vA, vB);
    t2 = dot(vB, vB);
    /* w2 = 1-fabs(t1/t2); */
    w2 = 1+t1/t2;

    /* ---------------- this is for P3 ---------------- */
    Side1 = Vector3d(Data->Vertices[Triangle->P2()] - Data->Vertices[Triangle->P1()]);
    Side2 = Vector3d(Data->Vertices[Triangle->P2()] - Data->Vertices[Triangle->P3()]);

    /* find A */
    vA = P - Vector3d(Data->Vertices[Triangle->P3()]);

    /* find B */
    t1 = dot(Side2, Side1);
    t2 = dot(Side1, Side1);
    vB = Side1 * (t1/t2) - Side2;

    t1 = dot(vA, vB);
    t2 = dot(vB, vB);
    /* w3 = 1-fabs(t1/t2); */
    w3 = 1+t1/t2;

    Result =  w1 * Vector2d(Data->UVCoords[UV_Index(Triangle, 0)]) +
              w2 * Vector2d(Data->UVCoords[UV_Index(Triangle, 1)]) +
              w3 * Vector2d(Data->UVCoords[UV_Index(Triangle, 2)]);
}


/*****************************************************************************
*
* FUNCTION
*
*   inside_bbox_tree
*
* INPUT
*
*   Mesh     - Mesh object
*   Ray      - Current ray
*
* OUTPUT
*
* RETURNS
*
*   bool - true if inside the object
*
* AUTHOR
*
*   Nathan Kopp & Dieter Bayer
*
* DESCRIPTION
*
*   Check if a point is within the bounding box tree of a mesh.
*
* CHANGES
*
*   Oct 1998 : Creation.
*
******************************************************************************/

bool Mesh::inside_bbox_tree(const BasicRay &ray, RenderStatistics& stats) const
{
    MeshIndex found = 0;
    DBL Best = BOUND_HUGE, Depth;

    Traverse_Flat_BBox_Tree(*Data->FlatTree, ray, Best, false, stats, [&](std::int32_t leaf) {
        if (intersect_mesh_triangle(ray, &Data->Triangles[leaf], &Depth))
            found++;
        return false;
    });
    return ((found & 1) != 0);
}

void Mesh::Determine_Textures(Intersection *isect, bool hitinside, WeightedTextureVector& textures, TraceThreadData *Threaddata)
{
    const MESH_TRIANGLE *tri = reinterpret_cast<const MESH_TRIANGLE *>(isect->Pointer);
    const size_t index = size_t(tri - Data->Triangles);

    if ((Interior_Texture != nullptr) && (hitinside == true)) // useful feature for checking mesh orientation and other effects [trf]
        textures.push_back(WeightedTexture(1.0, Interior_Texture));
    else if(tri->ThreeTex())
    {
        Vector3d p1, p2, p3;
        Vector3d epoint;
        COLC w1, w2, w3;
        COLC wsum;

        if (Trans != nullptr)
            MInvTransPoint(epoint, isect->IPoint, Trans);
        else
            epoint = isect->IPoint;

        p1 = Vector3d(Data->Vertices[tri->P1()]);
        p2 = Vector3d(Data->Vertices[tri->P2()]);
        p3 = Vector3d(Data->Vertices[tri->P3()]);

        w1 = 1.0 - COLC(SmoothTriangle::Calculate_Smooth_T(epoint, p1, p2, p3));
        w2 = 1.0 - COLC(SmoothTriangle::Calculate_Smooth_T(epoint, p2, p3, p1));
        w3 = 1.0 - COLC(SmoothTriangle::Calculate_Smooth_T(epoint, p3, p1, p2));

        wsum = 1.0 / (w1 + w2 + w3);

        textures.push_back(WeightedTexture(w1 * wsum, Textures[Data->TextureInd.Get(*tri, index, 0)]));
        textures.push_back(WeightedTexture(w2 * wsum, Textures[Data->Texture23Ind.Get(*tri, index, 0)]));
        textures.push_back(WeightedTexture(w3 * wsum, Textures[Data->Texture23Ind.Get(*tri, index, 1)]));
    }
    else if(Data->TextureInd.Get(*tri, index, 0) >= 0) // TODO FIXME - make sure there always is some valid texture, also for code above! [trf]
        textures.push_back(WeightedTexture(1.0, Textures[Data->TextureInd.Get(*tri, index, 0)]));
    else if (Texture != nullptr)
        textures.push_back(WeightedTexture(1.0, Texture));
}

}
// end of namespace pov
