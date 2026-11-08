//******************************************************************************
///
/// @file core/shape/csg.cpp
///
/// Implementation of constructive solid geometry (csg) shapes.
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
#include "core/shape/csg.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

// POV-Ray header files (base module)
#include "base/povassert.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/lighting/lightgroup.h"
#include "core/math/matrix.h"
#include "core/math/halfspacebounds.h"
#include "core/render/ray.h"
#include "core/lighting/lightsource.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/heightfield.h"
#include "core/shape/mesh.h"
#include "core/shape/plane.h"
#include "core/shape/quadric.h"
#include "core/shape/skein.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;
using std::vector;

static bool HalfspaceIntersectionBounds(const CSGIntersection& intersection, BoundingBox& bounds);
static bool FiniteCSGBounds(const CSG& object, BoundingBox& bounds);

static void RecomputeCSGBounds(CSG *object, const TRANSFORM *transform)
{
    Recompute_BBox(&object->BBox, transform);
    const CSGIntersection *intersection = dynamic_cast<const CSGIntersection *>(object);
    BoundingBox bounds;
    if (FiniteCSGBounds(*object, bounds) || (intersection && HalfspaceIntersectionBounds(*intersection, bounds)))
        object->BBox = bounds;
}

/*****************************************************************************
* Global preprocessor defines
******************************************************************************/

#define UNION_OBJECT        (IS_COMPOUND_OBJECT | IS_CSG_OBJECT)
#define MERGE_OBJECT        (IS_COMPOUND_OBJECT | IS_CSG_OBJECT)
#define INTERSECTION_OBJECT (IS_COMPOUND_OBJECT | IS_CSG_OBJECT)



inline bool Test_Ray_Flags(const Ray& ray, ConstObjectPtr obj)
{
    // CJC 2005 if ray is primary ray ignore NO_IMAGE_FLAG to support the trace() SDL function
    // TODO FIXME - I guess it would be better to have the trace() function use a different ray type [CLi]
    return ( ( !ray.IsPhotonRay() &&
               (!Test_Flag(obj, NO_IMAGE_FLAG) || ray.IsImageRay() == false || ray.IsPrimaryRay() == true) &&
               (!Test_Flag(obj, NO_REFLECTION_FLAG) || ray.IsReflectionRay() == false) &&
               (!Test_Flag(obj, NO_RADIOSITY_FLAG) || ray.IsRadiosityRay() == false) ) ||
             ( ray.IsPhotonRay() && !Hidden_From_Photons(obj) ) );
}

inline bool Test_Ray_Flags_Shadow(const Ray& ray, ConstObjectPtr obj)
{
    // TODO CLARIFY - why does this function not ignore NO_IMAGE_FLAG for primary rays, as Test_Ray_Flags() does? [CLi]
    return ( ( !ray.IsPhotonRay() &&
               (!Test_Flag(obj, NO_IMAGE_FLAG) || ray.IsImageRay() == false) &&
               (!Test_Flag(obj, NO_REFLECTION_FLAG) || ray.IsReflectionRay() == false) &&
               (!Test_Flag(obj, NO_RADIOSITY_FLAG) || ray.IsRadiosityRay() == false) ) ||
             ( ray.IsPhotonRay() && !Hidden_From_Photons(obj) ) ||
             ( ray.IsShadowTestRay() && !Test_Flag(obj, NO_SHADOW_FLAG) ) );
}

/// A CSG's children indexed by their padded world boxes, so a ray or a point asks only the children it can reach.
/// Built once on one thread after the scene's geometry is final and read-only afterwards; a geometry change drops it.
struct CSGChildTree final
{
    struct Box final
    {
        double lo[3], hi[3];
    };
    /// A leaf holds entries[first, first + count); an inner node (count 0) has its two children at nodes first and first + 1.
    struct Node final
    {
        Box box;
        std::uint32_t first, count;
    };
    struct Tree final
    {
        std::vector<Node> nodes;
        std::vector<std::uint32_t> entries;
        bool Built() const { return !nodes.empty(); }
    };
    /// What Inside_Object answers for a point outside a child's box.
    enum InsideKind : std::uint8_t { kInsideUnknown, kOutsideNotInside, kOutsideInside };

    static const unsigned kLeafSize = 4;
    static const unsigned kStackSize = 64;

    std::vector<Box> boxes;
    std::vector<std::uint8_t> insideKind;
    /// children whose every hit lies in their box; the rest are always asked
    Tree ray;
    std::vector<std::uint32_t> rayRest;
    /// unions and merges: the kOutsideNotInside children; intersections: the kOutsideInside ones; the rest in order
    Tree inside;
    std::vector<std::uint32_t> insideRest;

    static bool Contains(const Box& b, const Vector3d& p)
    {
        return (p[X] >= b.lo[X]) && (p[X] <= b.hi[X]) && (p[Y] >= b.lo[Y]) && (p[Y] <= b.hi[Y]) && (p[Z] >= b.lo[Z]) && (p[Z] <= b.hi[Z]);
    }

    /// Calls `leaf` on the entries of every leaf whose node passes `test`, until `leaf` returns true; returns whether it did.
    template <typename Test, typename Leaf>
    static bool Visit(const Tree& tree, const Test& test, const Leaf& leaf)
    {
        std::uint32_t stack[kStackSize];
        unsigned top = 0;
        stack[top++] = 0;
        while (top > 0)
        {
            const Node& node = tree.nodes[stack[--top]];
            if (!test(node.box))
                continue;
            if (node.count > 0)
            {
                for (std::uint32_t k = node.first; k < node.first + node.count; ++k)
                    if (leaf(tree.entries[k]))
                        return true;
            }
            else
            {
                stack[top++] = node.first;
                stack[top++] = node.first + 1;
            }
        }
        return false;
    }
};

namespace
{

/// A ray against child boxes: slabs in double, the inverse direction kept finite so an axis-parallel ray needs no special case.
class ChildBoxRay final
{
    public:
        ChildBoxRay() : valid(false) {}

        explicit ChildBoxRay(const Ray& ray) : valid(true)
        {
            for (int d = X; d <= Z; ++d)
            {
                origin[d] = ray.Origin[d];
                const DBL r = 1.0 / ray.Direction[d];
                inv[d] = (std::fabs(r) < 1.0e30) ? r : std::copysign(1.0e30, ray.Direction[d]);
                valid = valid && std::isfinite(ray.Origin[d]) && std::isfinite(ray.Direction[d]);
            }
        }

        /// Whether the ray, forward of its origin, may cross the box.
        bool Hits(const CSGChildTree::Box& b) const
        {
            DBL tn = -HUGE_VAL, tf = HUGE_VAL;
            for (int d = X; d <= Z; ++d)
            {
                const DBL t1 = (b.lo[d] - origin[d]) * inv[d], t2 = (b.hi[d] - origin[d]) * inv[d];
                tn = max(tn, min(t1, t2));
                tf = min(tf, max(t1, t2));
            }
            return (tn <= tf) && (tf >= 0.0);
        }

        bool valid;

    private:
        DBL origin[3], inv[3];
};

inline bool FinitePoint(const Vector3d& p)
{
    return std::isfinite(p[X]) && std::isfinite(p[Y]) && std::isfinite(p[Z]);
}

/// A light source without a shape takes no part in inside tests.
inline bool HasInside(ConstObjectPtr child)
{
    return !(child->Type & LIGHT_SOURCE_OBJECT) || !(reinterpret_cast<const LightSource *>(child))->children.empty();
}

/// Restores the candidate stack to its size on entry, so a nested CSG's candidates sit above this one's.
class CandidateScope final
{
    public:
        explicit CandidateScope(std::vector<unsigned int>& l) : list(l), base(l.size()) {}
        ~CandidateScope() { list.resize(base); }
        std::vector<unsigned int>& list;
        const size_t base;
};

/// Calls `visit` on every child a ray may hit, in the children's order: all of them without a tree, else the children whose box
/// the ray crosses and those the tree cannot hold.
template <typename Visit>
inline void ForEachRayChild(const CSG& csg, const Ray& ray, TraceThreadData *thread, const Visit& visit)
{
    const CSGChildTree *tree = csg.childTree.get();
    const bool indexed = (tree != nullptr) && tree->ray.Built();
    // set up only for a tree: most CSGs are small, and their rays should pay nothing for it
    const ChildBoxRay boxRay = indexed ? ChildBoxRay(ray) : ChildBoxRay();
    if (!boxRay.valid)
    {
        for (vector<ObjectPtr>::const_iterator child = csg.children.begin(); child != csg.children.end(); ++child)
            visit(child);
        return;
    }
    const CandidateScope scope(thread->csgCandidates);
    std::vector<unsigned int>& list = scope.list;
    CSGChildTree::Visit(tree->ray, [&boxRay](const CSGChildTree::Box& b) { return boxRay.Hits(b); },
                        [&](std::uint32_t k) { if (boxRay.Hits(tree->boxes[k])) list.push_back(k); return false; });
    // the children's order decides ties between equal depths, so the tree's hits merge with the rest in that order
    const size_t end = list.size();
    std::sort(list.begin() + scope.base, list.end());
    const std::vector<std::uint32_t>& rest = tree->rayRest;
    size_t i = scope.base, r = 0;
    while ((i < end) || (r < rest.size()))
    {
        // by index: a nested CSG may grow the list
        const unsigned int k = ((r < rest.size()) && ((i >= end) || (rest[r] < list[i]))) ? rest[r++] : list[i++];
        visit(csg.children.begin() + k);
    }
}

/// Whether `p` is inside a child other than `skip` that `asked` admits: the inside test of unions, and of merges per hit.
template <typename Asked>
inline bool AnyChildInside(const CSG& csg, const Vector3d& p, TraceThreadData *thread, ConstObjectPtr skip, const Asked& asked)
{
    const CSGChildTree *tree = csg.childTree.get();
    if ((tree == nullptr) || !tree->inside.Built() || !FinitePoint(p))
    {
        for (ObjectPtr child : csg.children)
            if ((child != skip) && HasInside(child) && asked(child) && Inside_Object(p, child, thread))
                return true;
        return false;
    }
    for (std::uint32_t k : tree->insideRest)
    {
        ObjectPtr child = csg.children[k];
        if ((child == skip) || !HasInside(child) || !asked(child))
            continue;
        if ((tree->insideKind[k] == CSGChildTree::kOutsideInside) && !CSGChildTree::Contains(tree->boxes[k], p))
            return true;
        if (Inside_Object(p, child, thread))
            return true;
    }
    return CSGChildTree::Visit(tree->inside, [&p](const CSGChildTree::Box& b) { return CSGChildTree::Contains(b, p); },
                               [&](std::uint32_t k)
                               {
                                   ObjectPtr child = csg.children[k];
                                   return (child != skip) && CSGChildTree::Contains(tree->boxes[k], p) && asked(child) &&
                                          Inside_Object(p, child, thread);
                               });
}

/// Whether `p` is inside every child other than `skip`: the inside test of intersections and differences, and theirs per hit.
inline bool AllChildrenInside(const CSG& csg, const Vector3d& p, TraceThreadData *thread, ConstObjectPtr skip)
{
    const CSGChildTree *tree = csg.childTree.get();
    if ((tree == nullptr) || !tree->inside.Built() || !FinitePoint(p))
    {
        for (ObjectPtr child : csg.children)
            if ((child != skip) && HasInside(child) && !Inside_Object(p, child, thread))
                return false;
        return true;
    }
    for (std::uint32_t k : tree->insideRest)
    {
        ObjectPtr child = csg.children[k];
        if ((child == skip) || !HasInside(child))
            continue;
        if ((tree->insideKind[k] == CSGChildTree::kOutsideNotInside) && !CSGChildTree::Contains(tree->boxes[k], p))
            return false;
        if (!Inside_Object(p, child, thread))
            return false;
    }
    // the tree holds inverted children, inside everywhere outside their boxes
    return !CSGChildTree::Visit(tree->inside, [&p](const CSGChildTree::Box& b) { return CSGChildTree::Contains(b, p); },
                                [&](std::uint32_t k)
                                {
                                    ObjectPtr child = csg.children[k];
                                    return (child != skip) && CSGChildTree::Contains(tree->boxes[k], p) && !Inside_Object(p, child, thread);
                                });
}

}

/*****************************************************************************
*
* FUNCTION
*
*   All_CSG_Union_Intersections
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
*   Sep 1994 : Added code to count intersection tests. [DB]
*
******************************************************************************/

bool CSGUnion::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    int Found;
    const IsoShadowWindow exactHits(Thread, 0.0, 0.0);

    Thread->Stats()[Ray_CSG_Union_Tests]++;

    Found = false;

    // Use shortcut if no clip.

    if(Clip.empty())
    {
        ForEachRayChild(*this, ray, Thread, [&](vector<ObjectPtr>::const_iterator Current_Sib)
        {
            if(Test_Ray_Flags(ray, (*Current_Sib))) // TODO CLARIFY - why does CSGUnion use Test_Ray_Flags(), while CSGMerge uses Test_Ray_Flags_Shadow(), and CSGIntersection uses neither?
            {
                if((*Current_Sib)->Bound.empty() == true || Ray_In_Bound(ray, (*Current_Sib)->Bound, Thread))
                {
                    if((*Current_Sib)->All_Intersections(ray, Depth_Stack, Thread))
                        Found = true;
                }
            }
        });
    }
    else
    {
        IStack Local_Stack(Thread->stackPool);
        POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack pulled from the pool is in a cleaned-up condition

        ForEachRayChild(*this, ray, Thread, [&](vector<ObjectPtr>::const_iterator Current_Sib)
        {
            if(Test_Ray_Flags(ray, (*Current_Sib))) // TODO CLARIFY - why does CSGUnion use Test_Ray_Flags(), while CSGMerge uses Test_Ray_Flags_Shadow(), and CSGIntersection uses neither?
            {
                if((*Current_Sib)->Bound.empty() == true || Ray_In_Bound(ray, (*Current_Sib)->Bound, Thread))
                {
                    if((*Current_Sib)->All_Intersections (ray, Local_Stack, Thread))
                    {
                        while(Local_Stack->size() > 0)
                        {
                            if(Clip.empty() || Point_In_Clip(Local_Stack->top().IPoint, Clip, Thread))
                            {
                                Local_Stack->top().Csg = this;

                                Depth_Stack->push(Local_Stack->top());

                                Found = true;
                            }

                            Local_Stack->pop();
                        }
                    }
                }
            }
        });
        POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack is in a cleaned-up condition (again)
    }

    if(Found)
        Thread->Stats()[Ray_CSG_Union_Tests_Succeeded]++;

    return (Found);
}



/*****************************************************************************
*
* FUNCTION
*
*   All_CSG_Intersection_Intersections
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
*   Sep 1994 : Added code to count intersection tests. [DB]
*
******************************************************************************/

bool CSGIntersection::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    int Maybe_Found, Found;
    const IsoShadowWindow exactHits(Thread, 0.0, 0.0);
    IStack Local_Stack(Thread->stackPool);
    POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack pulled from the pool is in a cleaned-up condition

    Thread->Stats()[Ray_CSG_Intersection_Tests]++;

    Found = false;

    ForEachRayChild(*this, ray, Thread, [&](vector<ObjectPtr>::const_iterator Current_Sib)
    {
        if ((*Current_Sib)->Bound.empty() == true || Ray_In_Bound(ray, (*Current_Sib)->Bound, Thread))
        {
            if((*Current_Sib)->All_Intersections(ray, Local_Stack, Thread))
            {
                while(Local_Stack->size() > 0)
                {
                    Maybe_Found = AllChildrenInside(*this, Local_Stack->top().IPoint, Thread, *Current_Sib);

                    if(Maybe_Found)
                    {
                        if(Clip.empty() || Point_In_Clip(Local_Stack->top().IPoint, Clip, Thread))
                        {
                            Local_Stack->top().Csg = this;

                            Depth_Stack->push(Local_Stack->top());

                            Found = true;
                        }
                    }

                    Local_Stack->pop();
                }
            }
        }
    });

    if(Found)
        Thread->Stats()[Ray_CSG_Intersection_Tests_Succeeded]++;

    POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack is in a cleaned-up condition (again)
    return (Found);
}



/*****************************************************************************
*
* FUNCTION
*
*   All_CSG_Merge_Intersections
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
*   Sep 1994 : Added code to count intersection tests. [DB]
*
******************************************************************************/

bool CSGMerge::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    int Found;
    const IsoShadowWindow exactHits(Thread, 0.0, 0.0);
    bool inside_flag;
    IStack Local_Stack(Thread->stackPool);
    POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack pulled from the pool is in a cleaned-up condition

    Thread->Stats()[Ray_CSG_Merge_Tests]++;

    Found = false;

    // FIXME - though the name is misleading, the OPTIMISE_SHADOW_TEST flag can be used to
    //  determine if we're in a shadow ray, but it SHOULD be renamed.
    // We should probably change Optimization_Flags to a "ray-type" variable, that will tell
    // us if it is primary, reflection, refraction, shadow, primary photon, photon refleciton, or photon refraction ray.
    int shadow_flag = ray.IsShadowTestRay(); // TODO FIXME - why is this flag not used?!

    ForEachRayChild(*this, ray, Thread, [&](vector<ObjectPtr>::const_iterator Sib1)
    {
        if ( Test_Ray_Flags_Shadow(ray, (*Sib1)) )// TODO CLARIFY - why does CSGUnion use Test_Ray_Flags(), while CSGMerge uses Test_Ray_Flags_Shadow(), and CSGIntersection uses neither?
        {
            if ((*Sib1)->Bound.empty() == true || Ray_In_Bound (ray, (*Sib1)->Bound, Thread))
            {
                if ((*Sib1)->All_Intersections (ray, Local_Stack, Thread))
                {
                    while (Local_Stack->size() > 0)
                    {
                        if (Clip.empty() || Point_In_Clip(Local_Stack->top().IPoint, Clip, Thread))
                        {
                            // a hit inside another child is not on the merge's surface
                            inside_flag = !AnyChildInside(*this, Local_Stack->top().IPoint, Thread, *Sib1,
                                                          [&ray](ConstObjectPtr Sib2) { return Test_Ray_Flags_Shadow(ray, Sib2); });

                            if (inside_flag == true)
                            {
                                Local_Stack->top().Csg = this;

                                Found = true;

                                Depth_Stack->push(Local_Stack->top());
                            }
                        }

                        Local_Stack->pop();
                    }
                }
            }
        }
    });

    if (Found)
        Thread->Stats()[Ray_CSG_Merge_Tests_Succeeded]++;

    POV_REFPOOL_ASSERT(Local_Stack->empty()); // verify that the IStack is in a cleaned-up condition (again)
    return (Found);
}



/*****************************************************************************
*
* FUNCTION
*
*   Inside_CSG_Union
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

bool CSGUnion::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    return AnyChildInside(*this, IPoint, Thread, nullptr, [](ConstObjectPtr) { return true; });
}



/*****************************************************************************
*
* FUNCTION
*
*   Inside_CSG_Intersection
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

bool CSGIntersection::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    return AllChildrenInside(*this, IPoint, Thread, nullptr);
}




/*****************************************************************************
*
* FUNCTION
*
*   Translate_CSG
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


void CSG::Translate(const Vector3d& Vector, const TRANSFORM *tr)
{
    for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        Translate_Object (*Current_Sib, Vector, tr) ;

    childTree.reset();
    RecomputeCSGBounds(this, tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Rotate_CSG
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


void CSG::Rotate(const Vector3d& Vector, const TRANSFORM *tr)
{
    for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        Rotate_Object (*Current_Sib, Vector, tr) ;

    childTree.reset();
    RecomputeCSGBounds(this, tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Scale_CSG
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


void CSG::Scale(const Vector3d& Vector, const TRANSFORM *tr)
{
    for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        Scale_Object (*Current_Sib, Vector, tr) ;

    childTree.reset();
    RecomputeCSGBounds(this, tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Transform_CSG
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


void CSG::Transform(const TRANSFORM *tr)
{
    for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        Transform_Object(*Current_Sib, tr);

    childTree.reset();
    RecomputeCSGBounds(this, tr);
}

/*****************************************************************************
*
* FUNCTION
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

ObjectPtr CSGUnion::Invert()
{
    ObjectPtr p = CompoundObject::Invert();
    POV_SHAPE_ASSERT(p == this);

    CSGIntersection *New = new CSGIntersection(false, *this, true);
    delete this;
    return (New);
}

ObjectPtr CSGIntersection::Invert()
{
    ObjectPtr p = CompoundObject::Invert();
    POV_SHAPE_ASSERT(p == this);

    CSGMerge *New = new CSGMerge(*this, true);
    delete this;
    return (New);
}

/*****************************************************************************
*
* FUNCTION
*
*   Create_CSG_Union
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
*   2000 : NK phmap
*
******************************************************************************/

CSGUnion::CSGUnion() : CSG(UNION_OBJECT)
{
    do_split = true;
}

CSGUnion::CSGUnion(int t) : CSG(t)
{
    do_split = true;
}



/*****************************************************************************
*
* FUNCTION
*
*   Create_CSG_Merge
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

CSGMerge::CSGMerge() : CSGUnion(MERGE_OBJECT)
{
}

CSGMerge::CSGMerge(CompoundObject& o, bool transplant) : CSGUnion(MERGE_OBJECT, o, transplant)
{
}

/*****************************************************************************
*
* FUNCTION
*
*   Create_CSG_Intersection
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

CSGIntersection::CSGIntersection(bool diff) : CSG(INTERSECTION_OBJECT), isDifference(diff)
{
    do_split = false; // TODO - not necessary but makes debugging clearer
}

CSGIntersection::CSGIntersection(bool diff, CompoundObject& o, bool transplant) : CSG(INTERSECTION_OBJECT, o, transplant), isDifference(diff)
{
    do_split = false; // TODO - not necessary but makes debugging clearer
}



/*****************************************************************************
*
* FUNCTION
*
*   Copy_CSG
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


ObjectPtr CSGUnion::Copy()
{
    CSGUnion *New = new CSGUnion();
    Destroy_Transform(New->Trans);
    *New = *this;
    New->childTree.reset();

    New->children.clear();
    New->children.reserve(children.size());
    for(vector<ObjectPtr>::iterator i(children.begin()); i != children.end(); i++)
        New->children.push_back(Copy_Object(*i));

    if(Type & LIGHT_GROUP_OBJECT)
    {
        New->lightGroupActive = Light_Group_Lights_Something(New);
        New->lightGroupCopy = true;
        New->PhotonLights.clear();
    }

    return (New);
}

ObjectPtr CSGMerge::Copy()
{
    CSGMerge *New = new CSGMerge();
    Destroy_Transform(New->Trans);
    *New = *this;
    New->childTree.reset();

    New->children.clear();
    New->children.reserve(children.size());
    for(vector<ObjectPtr>::iterator i(children.begin()); i != children.end(); i++)
        New->children.push_back(Copy_Object(*i));

    if(Type & LIGHT_GROUP_OBJECT)
    {
        New->lightGroupActive = Light_Group_Lights_Something(New);
        New->lightGroupCopy = true;
        New->PhotonLights.clear();
    }

    return (New);
}

ObjectPtr CSGIntersection::Copy()
{
    CSGIntersection *New = new CSGIntersection(false);
    Destroy_Transform(New->Trans);
    *New = *this;
    New->childTree.reset();

    New->children.clear();
    New->children.reserve(children.size());
    for(vector<ObjectPtr>::iterator i(children.begin()); i != children.end(); i++)
        New->children.push_back(Copy_Object(*i));

    if(Type & LIGHT_GROUP_OBJECT)
    {
        New->lightGroupActive = Light_Group_Lights_Something(New);
        New->lightGroupCopy = true;
        New->PhotonLights.clear();
    }

    return (New);
}

/*****************************************************************************
*
* FUNCTION
*
*   Compute_CSG_BBox
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
*   Sep 1994 : Improved bounding of quadrics used in CSG intersections. [DB]
*
******************************************************************************/

static bool HalfspaceIntersectionBounds(const CSGIntersection& intersection, BoundingBox& bounds)
{
    using namespace halfspace_bounds;
    if (intersection.isDifference || Test_Flag(&intersection, INVERTED_FLAG))
        return false;
    std::vector<halfspace_bounds::Plane> planes;
    for (ConstObjectPtr child : intersection.children)
    {
        const pov::Plane *plane = dynamic_cast<const pov::Plane *>(child);
        if (!plane || Test_Flag(child, INVERTED_FLAG))
            return false;
        halfspace_bounds::Plane equation;
        Interval translation(plane->Distance);
        for (int axis = 0; axis < 3; ++axis)
        {
            equation.normal[axis] = Interval(plane->Normal_Vector[axis]);
            if (plane->Trans)
            {
                equation.normal[axis] = Interval();
                for (int local = 0; local < 3; ++local)
                    equation.normal[axis] = equation.normal[axis] + Interval(plane->Normal_Vector[local]) *
                                            Interval(plane->Trans->inverse[axis][local]);
            }
        }
        if (plane->Trans)
            for (int local = 0; local < 3; ++local)
                translation = translation + Interval(plane->Normal_Vector[local]) * Interval(plane->Trans->inverse[3][local]);
        equation.interiorOffset = -translation;
        equation.offset = Interval(EPSILON) - translation;
        planes.push_back(equation);
    }
    std::array<double, 3> low, high;
    if (!Bounds(planes, low, high))
        return false;
    Make_BBox_from_min_max(bounds, Vector3d(low[0], low[1], low[2]), Vector3d(high[0], high[1], high[2]));
    return UsableFiniteBounds(bounds);
}

static bool FiniteCSGBounds(const CSG& object, BoundingBox& bounds)
{
    if (object.children.empty())
        return false;
    const CSGIntersection *intersection = dynamic_cast<const CSGIntersection *>(&object);
    const bool difference = intersection && intersection->isDifference;
    bool found = false;
    bool finiteInterior = !Test_Flag(&object, INVERTED_FLAG);
    Vector3d low, high;
    for (size_t index = 0; index < object.children.size(); ++index)
    {
        if (difference && index != 0)
            break;
        ConstObjectPtr child = object.children[index];
        if (!child->BBox.finite || (intersection && (Test_Flag(child, INVERTED_FLAG) || !child->BBox.finiteInterior)))
        {
            if (!intersection || difference)
                return false;
            continue;
        }
        finiteInterior = finiteInterior && child->BBox.finiteInterior && !Test_Flag(child, INVERTED_FLAG);
        Vector3d childLow, childHigh;
        Make_min_max_from_BBox(childLow, childHigh, child->BBox);
        if (!found)
        {
            low = childLow;
            high = childHigh;
            found = true;
        }
        else if (intersection)
        {
            low = max(low, childLow);
            high = min(high, childHigh);
        }
        else
        {
            low = min(low, childLow);
            high = max(high, childHigh);
        }
    }
    if (!found)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        if (high[axis] < low[axis])
            return false;
    Make_BBox_from_min_max(bounds, low, high);
    bounds.finiteInterior = bounds.finite && finiteInterior;
    return bounds.finite;
}

void CSG::Compute_BBox()
{
    DBL Old_Volume, New_Volume;
    Vector3d NewMin, NewMax, TmpMin, TmpMax, Min, Max;

    BoundingBox finiteBounds;
    if (FiniteCSGBounds(*this, finiteBounds))
    {
        BBox = finiteBounds;
        return;
    }
    CSGIntersection *intersection = dynamic_cast<CSGIntersection *>(this);
    if (intersection != nullptr) // FIXME
    {
        BoundingBox halfspaceBounds;
        if (HalfspaceIntersectionBounds(*intersection, halfspaceBounds))
        {
            BBox = halfspaceBounds;
            return;
        }
        /*
         * Calculate the bounding box of a CSG intersection
         * by intersecting the bounding boxes of all children.
         */

        NewMin = Vector3d(-BOUND_HUGE);
        NewMax = Vector3d(BOUND_HUGE);

        vector<Quadric *> Quadrics;

        /* Process all children. */

        for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        {
            /* Inverted objects and height fields mustn't be considered */

            if (!Test_Flag((*Current_Sib), INVERTED_FLAG) && (dynamic_cast<HField *>(*Current_Sib) == nullptr)) // FIXME
            {
                /* We store quadrics since they'll be processed last, to benefit from confining them to a certain range */
                if (dynamic_cast<Quadric *>(*Current_Sib) == nullptr) // FIXME
                {
                    if (dynamic_cast<Plane *>(*Current_Sib) != nullptr) // FIXME
                        Quadric::Compute_Plane_Min_Max(dynamic_cast<Plane *>(*Current_Sib), TmpMin, TmpMax);
                    else
                        Make_min_max_from_BBox(TmpMin, TmpMax, (*Current_Sib)->BBox);

                    NewMin = max(NewMin, TmpMin);
                    NewMax = min(NewMax, TmpMax);
                }
                else
                    Quadrics.push_back(dynamic_cast<Quadric *>(*Current_Sib));
            }
        }

        /* Process any quadrics. */

        for(vector<Quadric *>::iterator i = Quadrics.begin(); i != Quadrics.end(); i++)
        {
            Quadric *q = *i;

            Min = NewMin;
            Max = NewMax;

            q->Compute_BBox(Min, Max);

            Make_min_max_from_BBox(TmpMin, TmpMax, q->BBox);

            NewMin = max(NewMin, TmpMin);
            NewMax = min(NewMax, TmpMax);
        }
    }
    else
    {
        /* Calculate the bounding box of a CSG merge/union object. */

        NewMin = Vector3d(BOUND_HUGE);
        NewMax = Vector3d(-BOUND_HUGE);

        for(vector<ObjectPtr>::iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
        {
            Make_min_max_from_BBox(TmpMin, TmpMax, (*Current_Sib)->BBox);

            NewMin = min(NewMin, TmpMin);
            NewMax = max(NewMax, TmpMax);
        }
    }

    if((NewMin[X] > NewMax[X]) || (NewMin[Y] > NewMax[Y]) || (NewMin[Z] > NewMax[Z]))
        ;// TODO MESSAGE    Warning("Degenerate CSG bounding box (not used!).");
    else
    {
        New_Volume = (NewMax[X] - NewMin[X]) * (NewMax[Y] - NewMin[Y]) * (NewMax[Z] - NewMin[Z]);

        BOUNDS_VOLUME(Old_Volume, BBox);

        if(New_Volume < Old_Volume)
        {
            Make_BBox_from_min_max(BBox, NewMin, NewMax, false);

            /* Beware of bounding boxes too large. */

            if((BBox.size[X] > CRITICAL_LENGTH) ||
               (BBox.size[Y] > CRITICAL_LENGTH) ||
               (BBox.size[Z] > CRITICAL_LENGTH))
                Make_BBox(BBox, -BOUND_HUGE/2, -BOUND_HUGE/2, -BOUND_HUGE/2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE, false);
        }
    }
}



namespace
{

/// The child box padded out, wide enough for any hit or inside point a solver puts just past the certified box.
CSGChildTree::Box Padded_Box(const BoundingBox& box)
{
    CSGChildTree::Box b;
    for (int d = X; d <= Z; ++d)
    {
        const DBL lo = box.lowerLeft[d], hi = lo + DBL(box.size[d]);
        const DBL pad = 2.0e-4 + 4.0e-6 * (std::fabs(lo) + std::fabs(hi));
        b.lo[d] = lo - pad;
        b.hi[d] = hi + pad;
    }
    return b;
}

/// Whether the object's inside test turns its base domain inside out under INVERTED_FLAG. Shapes with no inside (patches,
/// meshes without inside_vector, open skeins, polygons) answer no inside however inverted, and a CSG inverts through its
/// children, so it holds only if each of them does, unclipped.
bool Honours_Inversion(ConstObjectPtr o)
{
    if (o->Type & (PATCH_OBJECT | NO_INSIDE_OBJECT))
        return false;
    if (const Mesh *mesh = dynamic_cast<const Mesh *>(o))
        return mesh->has_inside_vector;
    if (const Skein *skein = dynamic_cast<const Skein *>(o))
        return skein->data && skein->data->closed;
    if (const CompoundObject *compound = dynamic_cast<const CompoundObject *>(o))
        for (ConstObjectPtr child : compound->children)
            if (HasInside(child) && (!child->Clip.empty() || !Honours_Inversion(child)))
                return false;
    return true;
}

bool Usable_Box(const BoundingBox& box)
{
    if (!box.finite || box.isEmpty())
        return false;
    for (int d = X; d <= Z; ++d)
    {
        const DBL lo = box.lowerLeft[d], hi = lo + DBL(box.size[d]);
        if (!std::isfinite(lo) || !std::isfinite(hi) || (std::fabs(lo) > 1.0e15) || (std::fabs(hi) > 1.0e15))
            return false;
    }
    return true;
}

/// Splits items[first, first + count) at the median centroid of its longest axis; returns the depth reached.
unsigned Build_Node(CSGChildTree::Tree& tree, std::uint32_t index, std::uint32_t first, std::uint32_t count,
                    const std::vector<CSGChildTree::Box>& boxes)
{
    CSGChildTree::Box bounds = boxes[tree.entries[first]];
    DBL clo[3], chi[3];
    for (int d = X; d <= Z; ++d)
        clo[d] = chi[d] = bounds.lo[d] + bounds.hi[d];
    for (std::uint32_t k = first; k < first + count; ++k)
    {
        const CSGChildTree::Box& b = boxes[tree.entries[k]];
        for (int d = X; d <= Z; ++d)
        {
            bounds.lo[d] = min(bounds.lo[d], b.lo[d]);
            bounds.hi[d] = max(bounds.hi[d], b.hi[d]);
            clo[d] = min(clo[d], b.lo[d] + b.hi[d]);
            chi[d] = max(chi[d], b.lo[d] + b.hi[d]);
        }
    }
    tree.nodes[index].box = bounds;
    if (count <= CSGChildTree::kLeafSize)
    {
        std::sort(tree.entries.begin() + first, tree.entries.begin() + first + count);
        tree.nodes[index].first = first;
        tree.nodes[index].count = count;
        return 1;
    }
    int axis = X;
    for (int d = Y; d <= Z; ++d)
        if (chi[d] - clo[d] > chi[axis] - clo[axis])
            axis = d;
    const std::uint32_t half = count / 2;
    // ties broken by child index, so the tree is the same on every run
    std::nth_element(tree.entries.begin() + first, tree.entries.begin() + first + half, tree.entries.begin() + first + count,
                     [&boxes, axis](std::uint32_t a, std::uint32_t b)
                     {
                         const DBL ca = boxes[a].lo[axis] + boxes[a].hi[axis], cb = boxes[b].lo[axis] + boxes[b].hi[axis];
                         return (ca < cb) || ((ca == cb) && (a < b));
                     });
    const std::uint32_t left = std::uint32_t(tree.nodes.size());
    tree.nodes.resize(tree.nodes.size() + 2);
    tree.nodes[index].first = left;
    tree.nodes[index].count = 0;
    const unsigned depth = max(Build_Node(tree, left, first, half, boxes),
                               Build_Node(tree, left + 1, first + half, count - half, boxes));
    return depth + 1;
}

bool Build_Tree(CSGChildTree::Tree& tree, std::vector<std::uint32_t>&& items, const std::vector<CSGChildTree::Box>& boxes)
{
    tree.entries = std::move(items);
    tree.nodes.reserve(2 * (tree.entries.size() / CSGChildTree::kLeafSize) + 1);
    tree.nodes.resize(1);
    // a walk keeps at most one pending node per level plus the one in hand
    if (Build_Node(tree, 0, 0, std::uint32_t(tree.entries.size()), boxes) + 1 < CSGChildTree::kStackSize)
        return true;
    tree = CSGChildTree::Tree();
    return false;
}

}

unsigned CSG_Child_Tree_Minimum()
{
    // measured crossover: from 8 children the walk costs less than the scan for every leaf shape and arrangement measured;
    // at 4 it breaks even on spheres and boxes, and at 2 it costs a little more
    const unsigned kDefault = 8;
    const char *text = std::getenv("POV_CSG_CHILD_TREE");
    if ((text == nullptr) || (*text == '\0'))
        return kDefault;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if ((end == nullptr) || (*end != '\0'))
        return kDefault;
    return (value == 0) ? 0 : unsigned(max<unsigned long>(value, 2));
}

bool CSG::Prepare_Child_Tree(unsigned minimum)
{
    childTree.reset();
    if ((minimum == 0) || (children.size() < minimum) || (children.size() >= std::numeric_limits<std::uint32_t>::max() / 2))
        return false;
    const bool intersection = (dynamic_cast<const CSGIntersection *>(this) != nullptr);
    std::shared_ptr<CSGChildTree> tree = std::make_shared<CSGChildTree>();
    tree->boxes.resize(children.size());
    tree->insideKind.resize(children.size(), CSGChildTree::kInsideUnknown);
    std::vector<std::uint32_t> rayItems, insideItems;
    for (std::uint32_t k = 0; k < std::uint32_t(children.size()); ++k)
    {
        ConstObjectPtr child = children[k];
        const bool usable = !(child->Type & LIGHT_SOURCE_OBJECT) && Usable_Box(child->BBox);
        if (usable)
            tree->boxes[k] = Padded_Box(child->BBox);
        // the box holds every hit unless it is a bounded_by object's stand-in for a shape it does not enclose
        if (usable && (child->Bound.empty() || child->BBox.finiteInterior))
            rayItems.push_back(k);
        else
            tree->rayRest.push_back(k);
        // the box encloses the base inside domain (and a clipped child's clips); inversion turns outside into inside
        // only without clips, which still say no outside their own boxes, and only for shapes that honour it
        std::uint8_t& kind = tree->insideKind[k];
        if (usable && child->BBox.finiteInterior)
        {
            if (!Test_Flag(child, INVERTED_FLAG))
                kind = CSGChildTree::kOutsideNotInside;
            else if (child->Clip.empty() && Honours_Inversion(child))
                kind = CSGChildTree::kOutsideInside;
        }
        const std::uint8_t indexed = intersection ? CSGChildTree::kOutsideInside : CSGChildTree::kOutsideNotInside;
        if (kind == indexed)
            insideItems.push_back(k);
        else
            tree->insideRest.push_back(k);
    }
    bool any = false;
    if (rayItems.size() >= minimum)
        any = Build_Tree(tree->ray, std::move(rayItems), tree->boxes) || any;
    if (!tree->ray.Built())
        tree->rayRest.clear();
    if (insideItems.size() >= minimum)
        any = Build_Tree(tree->inside, std::move(insideItems), tree->boxes) || any;
    if (!tree->inside.Built())
        tree->insideRest.clear();
    if (any)
        childTree = std::move(tree);
    return any;
}

void Prepare_CSG_Child_Trees(ObjectPtr object, unsigned minimum, std::unordered_set<ConstObjectPtr>& visited, unsigned& built)
{
    // a leaf without clips or bounds holds no CSG: the common case, and cheap to pass over
    if ((object == nullptr) || (!(object->Type & IS_COMPOUND_OBJECT) && object->Clip.empty() && object->Bound.empty()))
        return;
    if (!visited.insert(object).second)
        return;
    for (ObjectPtr o : object->Clip)
        Prepare_CSG_Child_Trees(o, minimum, visited, built);
    for (ObjectPtr o : object->Bound)
        Prepare_CSG_Child_Trees(o, minimum, visited, built);
    if (LightSource *light = dynamic_cast<LightSource *>(object))
        Prepare_CSG_Child_Trees(light->Projected_Through_Object, minimum, visited, built);
    if (CompoundObject *compound = dynamic_cast<CompoundObject *>(object))
        for (ObjectPtr child : compound->children)
            Prepare_CSG_Child_Trees(child, minimum, visited, built);
    if (CSG *csg = dynamic_cast<CSG *>(object))
        built += csg->Prepare_Child_Tree(minimum) ? 1 : 0;
}

/*****************************************************************************
*
* FUNCTION
*
*   Determine_CSG_Textures
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

void CSG::Determine_Textures(Intersection *isect, bool hitinside, WeightedTextureVector& textures, TraceThreadData *threaddata)
{
    if(!children.empty())
    {
        if(Type & CSG_DIFFERENCE_OBJECT)
        {
            // For CSG Differences, use only the first object in the chain
            // (which is the first object in the POV file.  All other objects
            // are the ones that were "removed" from the first one, so their
            // textures should NOT be used.
            if(children[0]->Inside(isect->IPoint, threaddata))
            {
                if(children[0]->Type & IS_COMPOUND_OBJECT)
                    children[0]->Determine_Textures(isect, hitinside, textures, threaddata);
                else if (children[0]->Texture != nullptr)
                    textures.push_back(WeightedTexture(1.0, children[0]->Texture));
            }
        }
        else
        {
            size_t firstinserted = textures.size();

            for(vector<ObjectPtr>::const_iterator Current_Sib = children.begin(); Current_Sib != children.end(); Current_Sib++)
            {
                if((*Current_Sib)->Inside(isect->IPoint, threaddata))
                {
                    if((*Current_Sib)->Type & IS_COMPOUND_OBJECT)
                        (*Current_Sib)->Determine_Textures(isect, hitinside, textures, threaddata);
                    else if ((*Current_Sib)->Texture != nullptr)
                        textures.push_back(WeightedTexture(1.0, (*Current_Sib)->Texture));
                }
            }

            COLC weight = 1.0f / max(COLC(textures.size() - firstinserted), 1.0f);

            for(size_t i = firstinserted; i < textures.size(); i++)
                textures[i].weight = weight;
        }
    }
}

}
// end of namespace pov
