//******************************************************************************
///
/// @file core/shape/csg.h
///
/// Declarations related to constructive solid geometry (csg) shapes.
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

#ifndef POVRAY_CORE_CSG_H
#define POVRAY_CORE_CSG_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"
#include "core/shape/csg_fwd.h"

// C++ variants of C standard header files
// C++ standard header files
#include <memory>
#include <unordered_set>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/scene/object.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCoreShape
///
/// @{

/*****************************************************************************
* Global preprocessor defines
******************************************************************************/

/* CSG types */

#define CSG_UNION_TYPE             1
#define CSG_INTERSECTION_TYPE      2
#define CSG_DIFFERENCE_TYPE        4
#define CSG_MERGE_TYPE             8
#define CSG_SINGLE_TYPE           16



/*****************************************************************************
* Global typedefs
******************************************************************************/

struct CSGChildTree;
struct CSGSurfaceIndex;
struct CSGCells;

class CSG : public CompoundObject
{
    public:
        CSG(int t) : CompoundObject(t) {}
        CSG(int t, CompoundObject& o, bool transplant) : CompoundObject(t, o, transplant) {}

        int do_split;
        /// [light group] it held a light and an object as written, so the scene's final pass gives out its lights
        bool lightGroupActive = false;
        /// [light group] made by copying one, which leaves the group's own list of lights empty
        bool lightGroupCopy = false;
        /// Box trees over the children for rays and inside tests; null until Prepare_Child_Tree, below the size where they pay,
        /// and again after any change to the geometry.
        std::shared_ptr<const CSGChildTree> childTree;

        /// Builds `childTree` from the children's certified boxes when `minimum` of them can go in a tree (0: never);
        /// returns whether it did.
        bool Prepare_Child_Tree(unsigned minimum);
        /// Each child's first piece and leaf number, from the structure only; made on first use.
        mutable std::shared_ptr<const CSGSurfaceIndex> surfaceIndex;

        virtual std::uint32_t Surface_Leaves() const override;
        virtual bool Find_Surface_Leaf(ConstObjectPtr leafObject, const Vector3d& point, bool split, std::uint32_t& piece,
                                       std::uint32_t& leaf) const override;
        virtual ObjectPtr Surface_Leaf(std::uint32_t leaf) override;
        /// A CSG answers through its children, so it can answer more than its box when one of them can.
        virtual bool Has_Box_Test() const override;
        /// Calls `visit(context, child)` on each child the inside tree cannot rule out as outside at `p`, and on every child
        /// without a tree, until it returns true; returns whether it did.
        bool Any_Child_At(const Vector3d& p, bool (*visit)(void *context, ObjectPtr child), void *context) const;
        /// Calls `visit(context, child)` on each child the ray tree cannot rule out between depths nearDepth and farDepth of the
        /// ray, and on every child without a tree, in no set order.
        void Visit_Ray_Children(const Ray& ray, double nearDepth, double farDepth, void (*visit)(void *context, ObjectPtr child),
                                void *context) const;

        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const override { }
        virtual void Translate(const Vector3d&, const TRANSFORM *) override;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) override;
        virtual void Scale(const Vector3d&, const TRANSFORM *) override;
        virtual void Transform(const TRANSFORM *) override;
        virtual ObjectPtr Invert() override = 0;
        virtual void Compute_BBox() override;

        virtual void Determine_Textures(Intersection *isect, bool hitinside, WeightedTextureVector& textures, TraceThreadData *Threaddata) override;
};

/// The certified box's answer: a box missing it is outside an object that holds its inside there, and inside an inverted
/// one whose inside test honours the inversion; this is what the child trees assume of a child outside its box.
BoxClass Classify_By_Certified_Box(ConstObjectPtr object, const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld);

/// Child classifications cell refinement has made, all threads, since the program started.
std::uint64_t CSG_Cell_Tests();
/// Cell nodes alive, and their bytes.
std::uint64_t CSG_Cell_Nodes();

/// The child count from which a CSG indexes its children; POV_CSG_CHILD_TREE=<n> overrides it, 0 turns the trees off.
unsigned CSG_Child_Tree_Minimum();

/// Gives every CSG reachable from `object` its child tree; one thread, once the scene's geometry is final.
/// Adds the CSGs that got one to `built`.
void Prepare_CSG_Child_Trees(ObjectPtr object, unsigned minimum, std::unordered_set<ConstObjectPtr>& visited, unsigned& built);

class CSGUnion : public CSG
{
    public:
        CSGUnion();
        CSGUnion(int t);
        CSGUnion(int t, CompoundObject& o, bool transplant) : CSG(t, o, transplant) {}

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual ObjectPtr Invert() override;
        /// A union's members, and a merge's, are its pieces.
        virtual void Visit_Surface_Boxes(SurfaceBoxVisitor& visitor, std::uint32_t firstPiece, const SurfaceQuery& query = SurfaceQuery()) const override;
        /// The enclosure of the members' extents, within the union's own box.
        virtual bool Surface_Extent(double fineness, Vector3d& low, Vector3d& high) const override;
        virtual std::uint32_t Surface_Pieces() const override;
        /// Outside when every member is, inside when one is and none is unknown: a union reports its inner surfaces.
        virtual BoxClass Classify_Box(const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld) const override;
};

class CSGMerge final : public CSGUnion
{
    public:
        CSGMerge();
        CSGMerge(CompoundObject& o, bool transplant);

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        /// Outside when every member is, inside when any is: a merge drops the surface inside its other members.
        virtual BoxClass Classify_Box(const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld) const override;
};

class CSGIntersection final : public CSG
{
    public:
        CSGIntersection(bool diff);
        CSGIntersection(bool diff, CompoundObject& o, bool transplant);
        virtual ~CSGIntersection() override;

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual ObjectPtr Invert() override;
        /// Outside when any child is, inside when every child is; a difference's later children were inverted at parse.
        virtual BoxClass Classify_Box(const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld) const override;
        /// The cells left by refining the box as far as the query asks: one box per region, or the least-area cover.
        virtual void Visit_Surface_Boxes(SurfaceBoxVisitor& visitor, std::uint32_t firstPiece, const SurfaceQuery& query = SurfaceQuery()) const override;
        virtual bool Surface_Extent(double fineness, Vector3d& low, Vector3d& high) const override;

        /// Starts the cells in the frame the intersection has now, its build frame, when a child can answer more than its box.
        void Begin_Cells();
        /// Hands this object's children to the cells its copies share, if it is the one they classify with; before it goes.
        void Release_Cells();
        /// Whether requests refine cells here.
        bool Refines() const;
        /// The cell of the whole-set answer `answer` (at `fineness`) holding `point`, as an index into it; -1 if none.
        int Surface_Cell(const Vector3d& point, double fineness, const std::vector<SurfaceBox>& answer) const;

        bool isDifference;
        /// The cells, shared by copies; null: none.
        std::shared_ptr<CSGCells> cells;
        /// The transforms applied since the build frame; null: none.
        std::shared_ptr<const TRANSFORM> sinceBuild;
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_CSG_H
