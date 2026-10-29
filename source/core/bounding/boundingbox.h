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
#include <limits>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// C++ standard header files
#include <algorithm>
#include <functional>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/math/matrix.h"
#include "core/math/finite.h"
#include "core/math/halfspacebounds.h"
#include "core/math/simd.h"
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
    bool finite = false; ///< Certified finite surface enclosure.
    bool finiteInterior = false; ///< Certified base Inside-domain enclosure; consumers apply inversion.

    SNGL GetMinX() const { return lowerLeft.x(); }
    SNGL GetMinY() const { return lowerLeft.y(); }
    SNGL GetMinZ() const { return lowerLeft.z(); }

    SNGL GetMaxX() const { return lowerLeft.x() + size.x(); }
    SNGL GetMaxY() const { return lowerLeft.y() + size.y(); }
    SNGL GetMaxZ() const { return lowerLeft.z() + size.z(); }

    bool isEmpty() const { return (size.x() < 0) || (size.y() < 0) || (size.z() < 0); }
};

inline float NormalBBoxLower(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    return magnitude != 0 && magnitude < UINT32_C(0x00800000) ? -std::numeric_limits<float>::min() : value;
}
inline float NormalBBoxUpper(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    return magnitude != 0 && magnitude < UINT32_C(0x00800000) ? std::numeric_limits<float>::min() : value;
}
inline double NormalBBoxLower(double value) { return halfspace_bounds::WidenLow(value); }
inline double NormalBBoxUpper(double value) { return halfspace_bounds::WidenHigh(value); }

/// @relates BoundingBox
inline void Make_BBox_from_min_max(BoundingBox& BBox, const Vector3d& mins, const Vector3d& maxs, bool finite = true)
{
    BBox.finite = finite;
    for (int axis = 0; axis < 3; ++axis)
    {
        const DBL low = halfspace_bounds::WidenLow(mins[axis]);
        const DBL high = halfspace_bounds::WidenHigh(maxs[axis]);
        BBoxScalar origin = BBoxScalar(low);
        if (DBL(origin) > low)
            origin = std::nextafter(origin, -std::numeric_limits<BBoxScalar>::infinity());
        origin = NormalBBoxLower(origin);
        BBoxScalar upper = BBoxScalar(high);
        if (DBL(upper) < high)
            upper = std::nextafter(upper, std::numeric_limits<BBoxScalar>::infinity());
        upper = NormalBBoxUpper(upper);
        volatile DBL extent = DBL(upper) - DBL(origin);
        // the difference of two stored scalars is exact when their exponents are close enough for DBL to hold every bit
        const int gap = (origin == 0 || upper == 0) ? 0 : std::abs(std::ilogb(origin) - std::ilogb(upper));
        const bool exact = gap <= std::numeric_limits<DBL>::digits - 1 - std::numeric_limits<BBoxScalar>::digits;
        const DBL outwardExtent = (exact || (extent == 0.0 && low == high)) ? DBL(extent) :
                                   std::nextafter(DBL(extent), std::numeric_limits<DBL>::infinity());
        BBoxScalar size = BBoxScalar(outwardExtent);
        if (DBL(size) < outwardExtent)
            size = std::nextafter(size, std::numeric_limits<BBoxScalar>::infinity());
        size = NormalBBoxUpper(size);
        BBox.lowerLeft[axis] = origin;
        BBox.size[axis] = size;
        BBox.finite = BBox.finite && IsFiniteNumber(mins[axis]) && IsFiniteNumber(maxs[axis]) &&
                      IsFiniteNumber(DBL(origin)) && IsFiniteNumber(DBL(upper)) &&
                      IsFiniteNumber(DBL(size)) && size >= 0.0 && high >= low;
    }
    BBox.finiteInterior = BBox.finite;
}

/// @relates BoundingBox
inline void Make_BBox(BoundingBox& BBox, const DBL llx, const DBL lly, const DBL llz,
                      const DBL lex, const DBL ley, const DBL lez, bool finite = true)
{
    if (!finite)
    {
        BBox.lowerLeft = BBoxVector3d(llx, lly, llz);
        BBox.size = BBoxVector3d(lex, ley, lez);
        BBox.finite = false;
        BBox.finiteInterior = false;
        return;
    }
    const Vector3d low(llx, lly, llz), size(lex, ley, lez);
    Vector3d high;
    bool representable = true;
    for (int axis = 0; axis < 3; ++axis)
    {
        volatile DBL sum = low[axis] + size[axis];
        representable = representable && (!(size[axis] > 0.0) || DBL(sum) > low[axis]);
        // the sum's rounding error by two-sum: only a sum that rounded down needs the step up
        volatile DBL sizePart = sum - low[axis];
        volatile DBL lowPart = sum - sizePart;
        volatile DBL lowError = low[axis] - lowPart;
        volatile DBL sizeError = size[axis] - sizePart;
        high[axis] = size[axis] == 0.0 ? low[axis] :
                     (lowError + sizeError <= 0.0) ? DBL(sum) : std::nextafter(DBL(sum), std::numeric_limits<DBL>::infinity());
    }
    Make_BBox_from_min_max(BBox, low, high, representable);
}

/// @relates BoundingBox
inline void Make_BBox_from_min_max(BoundingBox& BBox, const BBoxVector3d& mins, const BBoxVector3d& maxs, bool finite = true)
{
    Make_BBox_from_min_max(BBox, Vector3d(mins), Vector3d(maxs), finite);
}

inline bool UsableFiniteBounds(const BoundingBox& box)
{
    if (!box.finite)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const DBL low = box.lowerLeft[axis], size = box.size[axis];
        const DBL high = low + size;
        const BBoxScalar storedHigh = box.lowerLeft[axis] + box.size[axis];
        if (!IsFiniteNumber(low) || !IsFiniteNumber(size) || !(size > 0.0) ||
            !IsFiniteNumber(high) || !(high > low) || !IsFiniteNumber(DBL(storedHigh)) || !(DBL(storedHigh) > low))
            return false;
    }
    return true;
}

inline void Make_Transformed_BBox(BoundingBox& box, const MATRIX& matrix)
{
    using halfspace_bounds::Interval;
    std::array<Interval, 3> coordinates;
    for (int axis = 0; axis < 3; ++axis)
    {
        const Interval upper = Interval(DBL(box.lowerLeft[axis])) + Interval(DBL(box.size[axis]));
        coordinates[axis] = Interval(DBL(box.lowerLeft[axis]), upper.high);
    }
    Vector3d low, high;
    for (int axis = 0; axis < 3; ++axis)
    {
        Interval result(matrix[3][axis]);
        for (int input = 0; input < 3; ++input)
            result = result + coordinates[input] * Interval(matrix[input][axis]);
        low[axis] = result.low;
        high[axis] = result.high;
    }
    const bool finiteInterior = box.finiteInterior;
    Make_BBox_from_min_max(box, low, high, box.finite);
    box.finiteInterior = box.finite && finiteInterior;
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
void Recompute_Inverse_BBox(BoundingBox *bbox, const TRANSFORM *trans);
bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread);
bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread);
void Check_And_Enqueue(BBoxPriorityQueue& Queue, const BBOX_TREE *Node, const BoundingBox *BBox, const Rayinfo *rayinfo, RenderStatistics& Stats, DBL maxDepth = HUGE_VAL);
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

/// A mesh's block: the child boxes as 16-bit steps of a grid spanning the block, rounded outward.
struct FlatQBBoxBlock final
{
    float origin[3], scale[3];
    std::uint16_t lo[3][FLAT_BBOX_WIDTH];
    std::uint16_t hi[3][FLAT_BBOX_WIDTH];
    std::int32_t child[FLAT_BBOX_WIDTH]; // as in FlatBBoxBlock
    std::uint16_t count;
    std::uint16_t more;
};

template<typename Block>
struct FlatBBoxTreeOf final
{
    std::vector<Block> blocks; // block 0 holds the root alone
    std::vector<const void *> leaves; // by leaf id; empty when ids index the caller's own array
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

typedef std::function<void(const std::uint32_t *items, size_t count)> BBoxGroupFn;
typedef std::function<void(size_t leaf, BoundingBox& box)> FlatLeafBoxFn;

/// One level of the bottom-up build: groups `boxes` by surface area, at most `bunching` unsplit, calling `emit` per
/// group; false once all form one.
bool Split_BBox_Pass(const BoundingBox *boxes, size_t count, int bunching, const BBoxGroupFn& emit);

FlatBBoxTree *Build_Flat_BBox_Tree(const BBOX_TREE *Root);
/// The tree Build_BBox_Tree would make of `numLeaves` finite leaves, flattened without building it; leaf k is id k.
FlatMeshBBoxTree *Build_Flat_BBox_Tree(size_t numLeaves, const FlatLeafBoxFn& leafBox);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread);
bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, const IntersectionStopCondition& stop, TraceThreadData *Thread);

/// A ray's box tests against a flat tree, and the entries they produce.
template<typename RayT>
class FlatBBoxWalker final
{
    public:
        FlatBBoxWalker(const RayT& ray, float minExit) : entries(local), capacity(LOCAL), size(0), minExit(minExit)
        {
            for (int d = 0; d < 3; ++d)
            {
                exactOrigin[d] = ray.Origin[d];
                direction[d] = ray.Direction[d];
                origin[d] = float(ray.Origin[d]);
                const DBL r = direction[d] == 0.0 ? 0.0 : 1.0 / direction[d];
                inv[d] = (fabs(r) < FLAT_BBOX_FAR) ? float(r) : std::copysign(FLAT_BBOX_FAR, float(ray.Direction[d]));
                originInv[d] = origin[d] * inv[d];
                safeBound[d] = (direction[d] == 0.0 || fabs(r) >= FLAT_BBOX_FAR) ? 0.0 :
                    std::numeric_limits<float>::max() / (4.0 * fabs(r));
            }
        }

        /// Appends the children of node `ref` whose boxes the ray enters before `maxDepth`.
        template<typename Block, typename StatsT>
        void Expand(const FlatBBoxTreeOf<Block>& tree, std::int32_t ref, float maxDepth, StatsT& stats)
        {
            const int base = size;
            const Lanes eps(minExit), limit(maxDepth);
            for (const Block *b = &tree.blocks[ref];; ++b)
            {
                Lanes tn, tf;
                Slabs(*b, tn, tf);
                std::uint32_t hit = simd::bits((tf >= tn) & (tf >= eps) & (tn <= limit)) & ((1u << b->count) - 1u);
                float depth[FLAT_BBOX_WIDTH];
                tn.store(depth);
                Reserve(b->count);
                for (; hit != 0; hit &= hit - 1u)
                {
                    const int k = simd::lowest_lane(hit);
                    entries[size++] = FlatBBoxEntry{b->child[k], depth[k]};
                }
                stats[nChecked] += b->count;
                if (!b->more)
                    break;
            }
            stats[nEnqueued] += size - base;
        }

        void Push(const FlatBBoxEntry& e) { Reserve(1); entries[size++] = e; }

        FlatBBoxEntry *entries;
        int capacity, size;

    private:
        static const int LOCAL = 256;
        float origin[3], inv[3], originInv[3];
        double exactOrigin[3], direction[3], safeBound[3];
        float minExit;

        typedef simd::Vec<float, FLAT_BBOX_WIDTH> Lanes;

        static void Bounds(const FlatBBoxBlock& b, int d, int k, double& lo, double& hi)
        {
            lo = b.lo[d][k];
            hi = b.hi[d][k];
        }

        static void Bounds(const FlatQBBoxBlock& b, int d, int k, double& lo, double& hi)
        {
            lo = std::nextafter(float(double(b.origin[d]) + double(b.lo[d][k]) * b.scale[d]), -std::numeric_limits<float>::infinity());
            hi = std::nextafter(float(double(b.origin[d]) + double(b.hi[d][k]) * b.scale[d]), std::numeric_limits<float>::infinity());
        }

        template<typename Block>
        void ScalarSlab(const Block& b, int d, Lanes& near, Lanes& far) const
        {
            float n[FLAT_BBOX_WIDTH], f[FLAT_BBOX_WIDTH];
            const float infinity = std::numeric_limits<float>::infinity();
            for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
            {
                double lo, hi;
                Bounds(b, d, k, lo, hi);
                if (direction[d] == 0.0)
                {
                    const bool inside = lo <= exactOrigin[d] && exactOrigin[d] <= hi;
                    n[k] = inside ? -infinity : infinity;
                    f[k] = inside ? infinity : -infinity;
                }
                else
                {
                    const double a = (lo - exactOrigin[d]) / direction[d], z = (hi - exactOrigin[d]) / direction[d];
                    n[k] = std::nextafter(float(std::min(a, z)), -infinity);
                    f[k] = std::nextafter(float(std::max(a, z)), infinity);
                }
            }
            near = Lanes::load(n);
            far = Lanes::load(f);
        }

        void Slabs(const FlatBBoxBlock& b, Lanes& tn, Lanes& tf) const
        {
            Lanes n[3], f[3];
            for (int d = 0; d < 3; ++d)
            {
                const Lanes safe{float(safeBound[d])};
                const bool scalar = fabs(exactOrigin[d]) >= safeBound[d] ||
                    simd::bits((simd::abs(Lanes::load(b.lo[d])) >= safe) | (simd::abs(Lanes::load(b.hi[d])) >= safe)) != 0;
                if (scalar)
                {
                    ScalarSlab(b, d, n[d], f[d]);
                    continue;
                }
                const Lanes o(origin[d]), r(inv[d]);
                const Lanes t1 = (Lanes::load(b.lo[d]) - o) * r, t2 = (Lanes::load(b.hi[d]) - o) * r;
                n[d] = simd::min(t1, t2);
                f[d] = simd::max(t1, t2);
            }
            tn = simd::max(simd::max(n[X], n[Y]), n[Z]);
            tf = simd::min(simd::min(f[X], f[Y]), f[Z]);
        }

        void Slabs(const FlatQBBoxBlock& b, Lanes& tn, Lanes& tf) const
        {
            Lanes n[3], f[3];
            for (int d = 0; d < 3; ++d)
            {
                if (fabs(exactOrigin[d]) >= safeBound[d] || fabs(b.origin[d]) >= safeBound[d] ||
                    fabs(double(b.scale[d]) * 65535.0) >= safeBound[d])
                {
                    ScalarSlab(b, d, n[d], f[d]);
                    continue;
                }
                const Lanes at(b.origin[d] * inv[d] - originInv[d]), step(b.scale[d] * inv[d]);
                const Lanes l = simd::fma(simd::load_widened<FLAT_BBOX_WIDTH>(b.lo[d]), step, at);
                const Lanes h = simd::fma(simd::load_widened<FLAT_BBOX_WIDTH>(b.hi[d]), step, at);
                n[d] = simd::min(h, l);
                f[d] = simd::max(l, h);
            }
            tn = simd::max(simd::max(n[X], n[Y]), n[Z]);
            tf = simd::min(simd::min(f[X], f[Y]), f[Z]);
        }

        FlatBBoxEntry local[LOCAL];
        std::vector<FlatBBoxEntry> spill;

        void Reserve(int more)
        {
            if (size + more <= capacity)
                return;
            if (entries == local)
                spill.assign(local, local + size);
            spill.resize(2 * (capacity + more));
            entries = spill.data();
            capacity = int(spill.size());
        }
};

/// Depth-first walk, nearer children first, calling `leaf` with the id of hit leaves starting before `best`, which it may
/// lower; true ends the walk. With `cull` false every hit leaf is visited. Boxes the ray leaves before `minExit` are skipped.
template<typename TreeT, typename RayT, typename StatsT, typename LeafFn>
void Traverse_Flat_BBox_Tree(const TreeT& tree, const RayT& ray, const DBL& best, bool cull, StatsT& stats, LeafFn&& leaf, float minExit = float(EPSILON))
{
    FlatBBoxWalker<RayT> w(ray, minExit);
    w.Push(FlatBBoxEntry{0, -FLAT_BBOX_FAR});
    while (w.size > 0)
    {
        const FlatBBoxEntry e = w.entries[--w.size];
        if (cull && e.depth > best)
            continue;
        if (e.ref < 0)
        {
            if (leaf(-1 - e.ref))
                return;
            continue;
        }

        const int base = w.size;
        w.Expand(tree, e.ref, cull ? float(std::min(best, DBL(FLAT_BBOX_FAR))) : FLAT_BBOX_FAR, stats);
        FlatBBoxEntry *hits = w.entries;
        std::sort(hits + base, hits + w.size, [](const FlatBBoxEntry& a, const FlatBBoxEntry& b) { return a.depth < b.depth; });

        // Leaves nearer than any sibling node are tested now, so their hits cull the rest; later ones queue behind the nodes.
        int nodes = base;
        for (int i = base; i < w.size; ++i)
        {
            const FlatBBoxEntry h = hits[i];
            if (cull && h.depth > best)
                break;
            if ((h.ref >= 0) || (nodes > base))
                hits[nodes++] = h;
            else if (leaf(-1 - h.ref))
                return;
        }
        std::reverse(hits + base, hits + nodes);
        w.size = nodes;
    }
}

/// The same with every box taken in order of entry depth across the whole tree, for costly leaves.
template<typename TreeT, typename RayT, typename StatsT, typename LeafFn>
void Traverse_Flat_BBox_Tree_Ordered(const TreeT& tree, const RayT& ray, const DBL& best, StatsT& stats, LeafFn&& leaf, float minExit = float(EPSILON))
{
    const auto later = [](const FlatBBoxEntry& a, const FlatBBoxEntry& b) { return a.depth > b.depth; };
    FlatBBoxWalker<RayT> w(ray, minExit);
    w.Push(FlatBBoxEntry{0, -FLAT_BBOX_FAR});
    while (w.size > 0)
    {
        std::pop_heap(w.entries, w.entries + w.size, later);
        const FlatBBoxEntry e = w.entries[--w.size];
        if (e.depth > best)
            break;
        if (e.ref < 0)
        {
            if (leaf(-1 - e.ref))
                return;
            continue;
        }
        const int base = w.size;
        w.Expand(tree, e.ref, float(std::min(best, DBL(FLAT_BBOX_FAR))), stats);
        for (int i = base + 1; i <= w.size; ++i)
            std::push_heap(w.entries, w.entries + i, later);
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
