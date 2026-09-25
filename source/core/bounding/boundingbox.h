//******************************************************************************
///
/// @file core/bounding/boundingbox.h
///
/// Declarations related to bounding boxes.
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

#ifndef POVRAY_CORE_BOUNDINGBOX_H
#define POVRAY_CORE_BOUNDINGBOX_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"
#include "core/bounding/boundingbox_fwd.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdint>

// C++ standard header files
#include <algorithm>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/math/matrix.h"
#include "core/render/ray_fwd.h"
#include "core/support/statisticids.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreBoundingBox Bounding Boxes
/// @ingroup PovCoreBounding
///
/// @{

struct RayObjectCondition;
class RenderStatistics;

/*****************************************************************************
* Global preprocessor defines
******************************************************************************/

/* Generate additional bbox statistics. */

#define BBOX_EXTRA_STATS 1


/*****************************************************************************
* Global typedefs
******************************************************************************/

typedef SNGL BBoxScalar;
typedef GenericVector3d<BBoxScalar> BBoxVector3d;

/// Structure holding bounding box data.
///
/// @note       The current implementation stores the data in lowerLeft/size format.
///
/// @todo       The reliability of the bounding mechanism could probably be improved by storing the
///             bounding data in min/max format rather than lowerLeft/size, and making sure
///             high-precision values are rounded towards positive/negative infinity as appropriate.
///
struct BoundingBox final
{
    BBoxVector3d lowerLeft;
    BBoxVector3d size;

    SNGL GetMinX() const { return lowerLeft.x(); }
    SNGL GetMinY() const { return lowerLeft.y(); }
    SNGL GetMinZ() const { return lowerLeft.z(); }

    SNGL GetMaxX() const { return lowerLeft.x() + size.x(); }
    SNGL GetMaxY() const { return lowerLeft.y() + size.y(); }
    SNGL GetMaxZ() const { return lowerLeft.z() + size.z(); }

    bool isEmpty() const { return (size.x() < 0) || (size.y() < 0) || (size.z() < 0); }
};

/// @relates BoundingBox
inline void Make_BBox(BoundingBox& BBox, const BBoxScalar llx, const BBoxScalar lly, const BBoxScalar llz, const BBoxScalar lex, const BBoxScalar ley, const BBoxScalar lez)
{
    BBox.lowerLeft = BBoxVector3d(llx, lly, llz);
    BBox.size      = BBoxVector3d(lex, ley, lez);
}

/// @relates BoundingBox
inline void Make_BBox_from_min_max(BoundingBox& BBox, const BBoxVector3d& mins, const BBoxVector3d& maxs)
{
    BBox.lowerLeft = mins;
    BBox.size      = maxs - mins;
}

/// @relates BoundingBox
inline void Make_BBox_from_min_max(BoundingBox& BBox, const Vector3d& mins, const Vector3d& maxs)
{
    BBox.lowerLeft = BBoxVector3d(mins);
    BBox.size      = BBoxVector3d(maxs - mins);
}

/// @relates BoundingBox
inline void Make_min_max_from_BBox(BBoxVector3d& mins, BBoxVector3d& maxs, const BoundingBox& BBox)
{
    mins = BBox.lowerLeft;
    maxs = mins + BBox.size;
}

/// @relates BoundingBox
inline void Make_min_max_from_BBox(Vector3d& mins, Vector3d& maxs, const BoundingBox& BBox)
{
    mins = Vector3d(BBox.lowerLeft);
    maxs = mins + Vector3d(BBox.size);
}

/// @relates BoundingBox
inline bool Inside_BBox(const Vector3d& point, const BoundingBox& bbox)
{
    if (point.x() < (DBL)bbox.lowerLeft.x())
        return(false);
    if (point.y() < (DBL)bbox.lowerLeft.y())
        return(false);
    if (point.z() < (DBL)bbox.lowerLeft.z())
        return(false);
    if (point.x() > (DBL)bbox.lowerLeft.x() + (DBL)bbox.size.x())
        return(false);
    if (point.y() > (DBL)bbox.lowerLeft.y() + (DBL)bbox.size.y())
        return(false);
    if (point.z() > (DBL)bbox.lowerLeft.z() + (DBL)bbox.size.z())
        return(false);

    return(true);
}

/// Structure holding bounding box data in min/max format.
///
struct MinMaxBoundingBox final
{
    BBoxVector3d pmin;
    BBoxVector3d pmax;
};

struct BBox_Tree_Struct final
{
    BBox_Tree_Struct **Node; // If node: children; if leaf: element
    BoundingBox BBox; // Bounding box of this node
    short Entries;    // Number of sub-nodes in this node
    bool Infinite;    // Flag if node is infinite
};

typedef bool VECTORB[3];

class Rayinfo final
{
    public:
        BBoxVector3d origin;        ///< Ray's origin.
        BBoxVector3d invDirection;  ///< Per-dimension inverse of the ray's direction.
        VECTORB nonzero;
        VECTORB positive;

        explicit Rayinfo(const BasicRay& ray)
        {
            DBL t;

            origin[X] = ray.Origin[X];
            origin[Y] = ray.Origin[Y];
            origin[Z] = ray.Origin[Z];

            if((nonzero[X] = ((t = ray.Direction[X]) != 0.0)) != 0)
            {
                invDirection[X] = 1.0 / t;
                positive[X] = (ray.Direction[X] > 0.0);
            }

            if((nonzero[Y] = ((t = ray.Direction[Y]) != 0.0)) != 0)
            {
                invDirection[Y] = 1.0 / t;
                positive[Y] = (ray.Direction[Y] > 0.0);
            }

            if((nonzero[Z] = ((t = ray.Direction[Z]) != 0.0)) != 0)
            {
                invDirection[Z] = 1.0 / t;
                positive[Z] = (ray.Direction[Z] > 0.0);
            }
        }
};

enum BBoxDirection
{
    BBOX_DIR_X0Y0Z0 = 0,
    BBOX_DIR_X0Y0Z1 = 1,
    BBOX_DIR_X0Y1Z0 = 2,
    BBOX_DIR_X0Y1Z1 = 3,
    BBOX_DIR_X1Y0Z0 = 4,
    BBOX_DIR_X1Y0Z1 = 5,
    BBOX_DIR_X1Y1Z0 = 6,
    BBOX_DIR_X1Y1Z1 = 7
};


/// Container for BBox subtrees prioritized by depth (distance to ray origin).
///
/// The current implementation is based on a so-called _min heap_ stored in a
/// (resizable) array, obeying the following rules:
///   - Element 0 is unused.
///   - If both element N and element 2*N are non-empty, element N has the
///     smaller depth of the two.
///   - If both element N and element 2*N+1 are non-empty, element N has the
///     smaller depth of the two.
///   - If element N is empty, so is element N+1.
/// This implementation guarantees execution time of O(log N) or better both
/// for inserting a new element and for extracting the one with the smallest
/// depth.
///
/// @note   We're using a custom priority queue class for now rather than
///         `std::priority_queue` becase we make use of Clear(), an operation
///         which `std::priority_queue` does not support.
///
class BBoxPriorityQueue final
{
    public:

        BBoxPriorityQueue();
        ~BBoxPriorityQueue();

        void Insert(DBL depth, ConstBBoxTreePtr node);
        bool RemoveMin(DBL& depth, ConstBBoxTreePtr& node);
        bool IsEmpty() const;
        void Clear();

    protected:

        struct Qelem final
        {
            DBL depth;
            ConstBBoxTreePtr node;
        };

        std::vector<Qelem> mQueue;
};


/*****************************************************************************
* Global functions
******************************************************************************/

void Build_BBox_Tree(BBOX_TREE **Root, size_t numOfFiniteObjects, BBOX_TREE **&Finite, size_t numOfInfiniteObjects, BBOX_TREE **Infinite, size_t& maxfinitecount);
void Build_Bounding_Slabs(BBOX_TREE **Root, std::vector<ObjectPtr>& objects, unsigned int& numberOfFiniteObjects, unsigned int& numberOfInfiniteObjects, unsigned int& numberOfLightSources);

void Recompute_BBox(BoundingBox *bbox, const TRANSFORM *trans);
bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread);
bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread);
void Check_And_Enqueue(BBoxPriorityQueue& Queue, const BBOX_TREE *Node, const BoundingBox *BBox, const Rayinfo *rayinfo, RenderStatistics& Stats, DBL maxDepth = BOUND_HUGE);
void Destroy_BBox_Tree(BBOX_TREE *Node);


// Flattened tree: a BBOX_TREE copied into blocks of eight child boxes, tested together and walked with a stack.

const int FLAT_BBOX_WIDTH = 8;
const float FLAT_BBOX_FAR = 1.0e30f;

struct FlatBBoxBlock final
{
    float lo[3][FLAT_BBOX_WIDTH];
    float hi[3][FLAT_BBOX_WIDTH];
    std::int32_t child[FLAT_BBOX_WIDTH]; // >= 0: node's first block; < 0: leaf -1-child
    std::int32_t count;
    std::int32_t more; // the node continues in the next block
};

struct FlatBBoxTree final
{
    std::vector<FlatBBoxBlock> blocks; // block 0 holds the root alone
    std::vector<const void *> leaves;
};

struct FlatBBoxEntry final
{
    std::int32_t ref;
    float depth;
};

struct IntersectionStopCondition
{
    virtual ~IntersectionStopCondition() {}
    virtual bool operator()(const Intersection& isect) const = 0;
};

FlatBBoxTree *Build_Flat_BBox_Tree(const BBOX_TREE *Root);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, const IntersectionStopCondition& stop, TraceThreadData *Thread);

/// Nearest-first walk calling `leaf` for hit leaves starting before `best`, which it may lower; true ends the walk.
/// With `cull` false every hit leaf is visited.
template<typename RayT, typename StatsT, typename LeafFn>
void Traverse_Flat_BBox_Tree(const FlatBBoxTree& tree, const RayT& ray, const DBL& best, bool cull, StatsT& stats, LeafFn&& leaf)
{
    float origin[3], inv[3];
    for (int d = 0; d < 3; ++d)
    {
        origin[d] = float(ray.Origin[d]);
        const DBL r = 1.0 / ray.Direction[d];
        inv[d] = (fabs(r) < FLAT_BBOX_FAR) ? float(r) : std::copysign(FLAT_BBOX_FAR, float(ray.Direction[d]));
    }

    const int LOCAL = 256;
    FlatBBoxEntry local[LOCAL];
    std::vector<FlatBBoxEntry> spill;
    FlatBBoxEntry *stack = local;
    int capacity = LOCAL, size = 0;
    stack[size++] = FlatBBoxEntry{0, -FLAT_BBOX_FAR};

    while (size > 0)
    {
        const FlatBBoxEntry e = stack[--size];
        if (cull && e.depth > best)
            continue;
        if (e.ref < 0)
        {
            if (leaf(tree.leaves[-1 - e.ref]))
                return;
            continue;
        }

        const float maxDepth = cull ? float(std::min(best, DBL(FLAT_BBOX_FAR))) : FLAT_BBOX_FAR;
        const int base = size;
        for (const FlatBBoxBlock *b = &tree.blocks[e.ref];; ++b)
        {
            float tn[FLAT_BBOX_WIDTH], tf[FLAT_BBOX_WIDTH];
            for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
            {
                const float x1 = (b->lo[X][k] - origin[X]) * inv[X], x2 = (b->hi[X][k] - origin[X]) * inv[X];
                const float y1 = (b->lo[Y][k] - origin[Y]) * inv[Y], y2 = (b->hi[Y][k] - origin[Y]) * inv[Y];
                const float z1 = (b->lo[Z][k] - origin[Z]) * inv[Z], z2 = (b->hi[Z][k] - origin[Z]) * inv[Z];
                tn[k] = std::max(std::max(std::min(x1, x2), std::min(y1, y2)), std::min(z1, z2));
                tf[k] = std::min(std::min(std::max(x1, x2), std::max(y1, y2)), std::max(z1, z2));
            }
            if (size + b->count > capacity)
            {
                if (stack == local)
                    spill.assign(local, local + size);
                spill.resize(2 * (capacity + b->count));
                stack = spill.data();
                capacity = int(spill.size());
            }
            for (int k = 0; k < b->count; ++k)
                if ((tf[k] >= tn[k]) && (tf[k] >= float(EPSILON)) && (tn[k] <= maxDepth))
                    stack[size++] = FlatBBoxEntry{b->child[k], tn[k]};
            stats[nChecked] += b->count;
            if (!b->more)
                break;
        }
        stats[nEnqueued] += size - base;
        std::sort(stack + base, stack + size, [](const FlatBBoxEntry& a, const FlatBBoxEntry& b) { return a.depth < b.depth; });

        // Leaves nearer than any sibling node are tested now, so their hits cull the rest; later ones queue behind the nodes.
        int nodes = base;
        for (int i = base; i < size; ++i)
        {
            const FlatBBoxEntry h = stack[i];
            if (cull && h.depth > best)
                break;
            if ((h.ref >= 0) || (nodes > base))
                stack[nodes++] = h;
            else if (leaf(tree.leaves[-1 - h.ref]))
                return;
        }
        std::reverse(stack + base, stack + nodes);
        size = nodes;
    }
}


/*****************************************************************************
* Inline functions
******************************************************************************/

// Calculate the volume of a bounding box. [DB 8/94]
inline void BOUNDS_VOLUME(DBL& a, const BoundingBox& b)
{
    a = b.size[X] * b.size[Y] * b.size[Z];
}

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_BOUNDINGBOX_H
