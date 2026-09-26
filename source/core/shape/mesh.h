//******************************************************************************
///
/// @file core/shape/mesh.h
///
/// Declarations related to the mesh geometric primitive.
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

#ifndef POVRAY_CORE_MESH_H
#define POVRAY_CORE_MESH_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdint>
#include <memory>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/bounding/boundingbox_fwd.h"
#include "core/scene/object.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCoreShape
///
/// @{

//******************************************************************************
///
/// @name Object Types
///
/// @{

#define MESH_OBJECT (PATCH_OBJECT+HIERARCHY_OK_OBJECT) // NOTE: During parsing, the PATCH_OBJECT type flag may be cleared if an inside_vector is specified

/// @}
///
//******************************************************************************

/*****************************************************************************
* Global typedefs
******************************************************************************/

// TODO - a SnglVector2d should probably suffice for MeshUVVector, and reduce the Mesh's memory footprint by 8 bytes per UV coordinate.

using MeshVector    = SnglVector3d; ///< Data type used to store vertices and normals.
using MeshUVVector  = Vector2d;     ///< Data type used to store UV coordinates.
using MeshIndex     = signed int;   ///< Data type used to store indices into vertices / normals / uv coordinate / texture tables. Must be signed and able to hold 2*max.

const MeshIndex MESH_MAX_VERTICES = MeshIndex(1) << 30; ///< Vertex indices share their word with the triangle's flags.

/// A triangle: its vertex indices, with its flags in their top bits; the plane and smoothing frame come from the vertices.
struct Mesh_Triangle_Struct final
{
    std::uint32_t Word[3];

    static const std::uint32_t INDEX_MASK = (std::uint32_t(1) << 30) - 1;

    MeshIndex P(int i) const { return MeshIndex(Word[i] & INDEX_MASK); }
    MeshIndex P1() const { return P(0); }
    MeshIndex P2() const { return P(1); }
    MeshIndex P3() const { return P(2); }
    void SetP(int i, MeshIndex v) { Word[i] = (Word[i] & ~INDEX_MASK) | std::uint32_t(v); }

    int Dominant_Axis() const { return int(Word[0] >> 30); }
    bool Smooth() const { return (Word[1] >> 31) != 0; }
    bool ThreeTex() const { return ((Word[1] >> 30) & 1) != 0; }
    bool Flipped() const { return (Word[2] >> 31) != 0; } ///< Vertices 1 and 2 were swapped, so the face normal is reversed.

    void SetDominantAxis(int a) { Word[0] = (Word[0] & INDEX_MASK) | (std::uint32_t(a) << 30); }
    void SetSmooth(bool b) { SetFlag(1, 31, b); }
    void SetThreeTex(bool b) { SetFlag(1, 30, b); }
    void SetFlipped(bool b) { SetFlag(2, 31, b); }

private:
    void SetFlag(int w, int bit, bool b) { Word[w] = (Word[w] & ~(std::uint32_t(1) << bit)) | (std::uint32_t(b) << bit); }
};
using MESH_TRIANGLE = Mesh_Triangle_Struct; ///< @deprecated

/// Indices a triangle may carry beyond its vertices, `width` per triangle, stored only once triangles differ.
struct MeshIndexColumn final
{
    std::vector<MeshIndex> values; ///< Empty while every entry is `fill`, or equals its vertex index if `byVertex`.
    MeshIndex fill;
    bool byVertex;
    int width;

    MeshIndexColumn(int w, MeshIndex f) : fill(f), byVertex(false), width(w) {}

    MeshIndex Get(const Mesh_Triangle_Struct& t, size_t tri, int k) const
    {
        return !values.empty() ? values[tri * width + k] : byVertex ? t.P(k) : fill;
    }
    void Set(size_t tri, int k, MeshIndex v);
    void Swap(size_t tri, int j, int k);
    /// Sizes the column to `n` triangles, dropping it if the triangles `relevant` accepts hold a constant or their vertex indices.
    void Finish(const Mesh_Triangle_Struct *triangles, size_t n, bool vertexLike, bool (*relevant)(const Mesh_Triangle_Struct&));
};

struct Mesh_Data_Struct final
{
    int References = 0;                     ///< Number of references to the mesh.
    MeshIndex Number_Of_UVCoords = 0;       ///< Number of UV coords in the mesh.
    MeshIndex Number_Of_Normals = 0;        ///< Number of normals in the mesh.
    MeshIndex Number_Of_Triangles = 0;      ///< Number of trinagles in the mesh.
    MeshIndex Number_Of_Vertices = 0;       ///< Number of vertices in the mesh.
    MeshVector *Normals = nullptr;          ///< Smooth triangles' vertex normals.
    MeshVector *Vertices = nullptr;
    MeshUVVector *UVCoords = nullptr;       ///< Array of UV coordinates
    MESH_TRIANGLE *Triangles = nullptr;     ///< Array of triangles.
    MeshIndexColumn NormalInd {3, -1};      ///< Smooth triangles' normal indices.
    MeshIndexColumn UVInd {3, 0};
    MeshIndexColumn TextureInd {1, -1};
    MeshIndexColumn Texture23Ind {2, -1};   ///< The second and third texture of a colour-interpolated triangle.
    FlatBBoxTree *FlatTree = nullptr;       ///< Bounding box tree for mesh, flattened; leaf ids are triangle indices.
    Vector3d Inside_Vect;                   ///< vector to use to test 'inside'
};
using MESH_DATA = Mesh_Data_Struct; ///< @deprecated

struct Hash_Table_Struct final
{
    MeshIndex Index;
    MeshVector P;
    Hash_Table_Struct *Next;
};
using HASH_TABLE = Hash_Table_Struct; ///< @deprecated

struct UV_Hash_Table_Struct final
{
    MeshIndex Index;
    MeshUVVector P;
    UV_Hash_Table_Struct *Next;
};
using UV_HASH_TABLE = UV_Hash_Table_Struct; ///< @deprecated

class Mesh final : public ObjectBase
{
    public:
        MESH_DATA *Data;                ///< Mesh data holding triangles.
        MeshIndex Number_Of_Textures;   ///< Number of textures in the mesh.
        TEXTURE **Textures;             ///< Array of texture references.
        bool has_inside_vector;

        Mesh();
        virtual ~Mesh() override;

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const override;
        virtual void UVCoord(Vector2d&, const Intersection *) const override;
        virtual void Translate(const Vector3d&, const TRANSFORM *) override;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) override;
        virtual void Scale(const Vector3d&, const TRANSFORM *) override;
        virtual void Transform(const TRANSFORM *) override;
        virtual void Compute_BBox() override;
        virtual bool IsOpaque() const override;

        void Create_Mesh_Hash_Tables();

        /// @note The method may decide to re-order the vertices without notice.
        bool Compute_Mesh_Triangle(MESH_TRIANGLE *Triangle, MeshIndex Index, bool Smooth, const Vector3d& P1, const Vector3d& P2, const Vector3d& P3);
        /// Drops the index columns that carry no information once every triangle is in.
        void Finish_Mesh_Data();

        void Build_Mesh_BBox_Tree();
        bool Degenerate(const Vector3d& P1, const Vector3d& P2, const Vector3d& P3);
        void Init_Mesh_Triangle(MESH_TRIANGLE *Triangle);
        void Destroy_Mesh_Hash_Tables();
        MeshIndex Mesh_Hash_Vertex(MeshIndex *Number_Of_Vertices, MeshIndex *Max_Vertices, MeshVector **Vertices, const Vector3d& Vertex);
        MeshIndex Mesh_Hash_Normal(MeshIndex *Number_Of_Normals, MeshIndex *Max_Normals, MeshVector **Normals, const Vector3d& Normal);
        MeshIndex Mesh_Hash_Texture(MeshIndex *Number_Of_Textures, MeshIndex *Max_Textures, TEXTURE ***Textures, TEXTURE *Texture);
        MeshIndex Mesh_Hash_UV(MeshIndex *Number, MeshIndex *Max, MeshUVVector **Elements, const Vector2d& aPoint);
        void Smooth_Mesh_Normal(Vector3d& Result, const MESH_TRIANGLE *Triangle, const Vector3d& IPoint) const;
        /// The triangle's unit normal, as the vertices first gave it.
        Vector3d Face_Normal(const MESH_TRIANGLE *Triangle) const;
        MeshIndex UV_Index(const MESH_TRIANGLE *Triangle, int k) const { return Data->UVInd.Get(*Triangle, size_t(Triangle - Data->Triangles), k); }

        virtual void Determine_Textures(Intersection *, bool, WeightedTextureVector&, TraceThreadData *) override;
    protected:
        bool Intersect(const BasicRay& ray, IStack& Depth_Stack, TraceThreadData *Thread);
        void Compute_Mesh_BBox();
        void MeshUV(const Vector3d& P, const MESH_TRIANGLE *Triangle, Vector2d& Result) const;
        bool intersect_mesh_triangle(const BasicRay& ray, const MESH_TRIANGLE *Triangle, DBL *Depth) const;
        bool test_hit(const MESH_TRIANGLE *Triangle, const BasicRay& OrigRay, DBL Depth, DBL len, IStack& Depth_Stack, TraceThreadData *Thread);
        void get_triangle_bbox(const MESH_TRIANGLE *Triangle, BoundingBox *BBox) const;
        bool intersect_bbox_tree(const BasicRay& ray, const BasicRay& Orig_Ray, DBL len, IStack& Depth_Stack, TraceThreadData *Thread);
        bool inside_bbox_tree(const BasicRay& ray, RenderStatistics& stats) const;
        void get_triangle_vertices(const MESH_TRIANGLE *Triangle, Vector3d& P1, Vector3d& P2, Vector3d& P3) const;
        void get_triangle_normals(const MESH_TRIANGLE *Triangle, Vector3d& N1, Vector3d& N2, Vector3d& N3) const;
        void get_triangle_uvcoords(const MESH_TRIANGLE *Triangle, Vector2d& U1, Vector2d& U2, Vector2d& U3) const;
        static MeshIndex mesh_hash(HASH_TABLE **Hash_Table, MeshIndex *Number, MeshIndex *Max, MeshVector **Elements, const Vector3d& aPoint);

private:
        // these are used temporarily during parsing and are destroyed
        // when the parser has finished constructing the object
        static HASH_TABLE **Vertex_Hash_Table;
        static HASH_TABLE **Normal_Hash_Table;
        static UV_HASH_TABLE **UV_Hash_Table;
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_MESH_H
