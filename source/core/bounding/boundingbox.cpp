//******************************************************************************
///
/// @file core/bounding/boundingbox.cpp
///
/// Implementations related to bounding boxes.
///
/// @author Mark VandeWettering (original idea)
/// @author Alexander Enzmann (adaptation to POV-Ray)
/// @author Eric Haines (optimizations)
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
#include "core/bounding/boundingbox.h"

// C++ variants of C standard header files
#include <algorithm>
#include <cstdlib>
#include <cstring>

// C++ standard header files
//  (none at the moment)

// POV-Ray header files (base module)
#include "base/pov_err.h"

// POV-Ray header files (core module)
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::vector;

const int BUNCHING_FACTOR = 4;
// Initial number of entries in a priority queue.
const int INITIAL_PRIORITY_QUEUE_SIZE = 256;
const int BBQ_FIRST_ELEMENT = 1;

BBOX_TREE *create_bbox_node(int size);

void calc_bbox(BoundingBox *BBox, BBOX_TREE **Finite, ptrdiff_t first, ptrdiff_t last);
bool split_pass(BBOX_TREE **Root, BBOX_TREE **&Finite, size_t *numOfFiniteObjects, ptrdiff_t first, ptrdiff_t last, size_t& maxfinitecount);

BBoxPriorityQueue::BBoxPriorityQueue()
{
    // Pre-reserve heap capacity: avoids repeated realloc/default-append on Insert
    // (profile: tens of millions of Inserts per frame on complex scenes).
    mQueue.reserve(1024);
    mQueue.resize(BBQ_FIRST_ELEMENT); // element 0 is reserved
}

BBoxPriorityQueue::~BBoxPriorityQueue()
{}

void BBoxPriorityQueue::Insert(DBL depth, ConstBBoxTreePtr node)
{
    // Grow with push_back (no default-fill of intermediate elements).
    mQueue.push_back(Qelem());
    vector<Qelem>::size_type i = mQueue.size() - 1;

    while ((i > BBQ_FIRST_ELEMENT) && (depth < mQueue[i / 2].depth))
    {
        mQueue[i] = mQueue[i / 2];
        i /= 2;
    }
    mQueue[i].depth = depth;
    mQueue[i].node  = node;
}

bool BBoxPriorityQueue::RemoveMin(DBL& depth, ConstBBoxTreePtr& node)
{
    vector<Qelem>::size_type size = mQueue.size() - 1;
    vector<Qelem>::size_type i, j;

    if (size == 0)
        return false;

    depth = mQueue[BBQ_FIRST_ELEMENT].depth;
    node  = mQueue[BBQ_FIRST_ELEMENT].node;

    // Last element to re-insert into the heap (before pop).
    const Qelem last = mQueue[size];
    mQueue.pop_back();
    size = mQueue.size() - 1; // new last index; 0 if only sentinel remains

    if (size == 0)
        return true;

    i = BBQ_FIRST_ELEMENT;

    while (i <= size / 2) // equivalent to 2*i <= size, but more robust
    {
        if ((2 * i == size) || (mQueue[2 * i].depth < mQueue[2 * i + 1].depth))
            j = 2 * i;
        else
            j = 2 * i + 1;

        if (last.depth <= mQueue[j].depth)
            break;

        mQueue[i] = mQueue[j];
        i = j;
    }
    mQueue[i] = last;

    return true;
}

bool BBoxPriorityQueue::IsEmpty() const
{
    return (mQueue.size() == BBQ_FIRST_ELEMENT);
}

void BBoxPriorityQueue::Clear()
{
    // Keep capacity; only reset logical size (C++11+ resize-down preserves capacity).
    mQueue.resize(BBQ_FIRST_ELEMENT);
}

void Destroy_BBox_Tree(BBOX_TREE *Node)
{
    if (Node != nullptr)
    {
        if(Node->Entries > 0)
        {
            for(short i = 0; i < Node->Entries; i++)
                Destroy_BBox_Tree(Node->Node[i]);

            POV_FREE(Node->Node);

            Node->Entries = 0;
            Node->Node = nullptr;
        }

        POV_FREE(Node);
    }
}

void Recompute_BBox(BoundingBox *bbox, const TRANSFORM *trans)
{
    int i;
    Vector3d lower_left, lengths, corner;
    Vector3d mins, maxs;

    if (trans == nullptr)
        return;

    lower_left = Vector3d(bbox->lowerLeft);
    lengths    = Vector3d(bbox->size);

    mins = Vector3d(BOUND_HUGE);
    maxs = Vector3d(-BOUND_HUGE);

    for(i = 1; i <= 8; i++)
    {
        corner = lower_left;

        corner[X] += ((i & 1) ? lengths[X] : 0.0);
        corner[Y] += ((i & 2) ? lengths[Y] : 0.0);
        corner[Z] += ((i & 4) ? lengths[Z] : 0.0);

        MTransPoint(corner, corner, trans);

        if(corner[X] < mins[X]) { mins[X] = corner[X]; }
        if(corner[X] > maxs[X]) { maxs[X] = corner[X]; }
        if(corner[Y] < mins[Y]) { mins[Y] = corner[Y]; }
        if(corner[Y] > maxs[Y]) { maxs[Y] = corner[Y]; }
        if(corner[Z] < mins[Z]) { mins[Z] = corner[Z]; }
        if(corner[Z] > maxs[Z]) { maxs[Z] = corner[Z]; }
    }

    // Clip bounding box at the largest allowed bounding box.
    if(mins[X] < -BOUND_HUGE / 2) { mins[X] = -BOUND_HUGE / 2; }
    if(mins[Y] < -BOUND_HUGE / 2) { mins[Y] = -BOUND_HUGE / 2; }
    if(mins[Z] < -BOUND_HUGE / 2) { mins[Z] = -BOUND_HUGE / 2; }
    if(maxs[X] >  BOUND_HUGE / 2) { maxs[X] =  BOUND_HUGE / 2; }
    if(maxs[Y] >  BOUND_HUGE / 2) { maxs[Y] =  BOUND_HUGE / 2; }
    if(maxs[Z] >  BOUND_HUGE / 2) { maxs[Z] =  BOUND_HUGE / 2; }

    Make_BBox_from_min_max(*bbox, mins, maxs);
}

void Recompute_Inverse_BBox(BoundingBox *bbox, const TRANSFORM *trans)
{
    int i;
    Vector3d lower_left, lengths, corner;
    Vector3d mins, maxs;

    if (trans == nullptr)
        return;

    lower_left = Vector3d(bbox->lowerLeft);
    lengths    = Vector3d(bbox->size);

    mins = Vector3d(BOUND_HUGE);
    maxs = Vector3d(-BOUND_HUGE);

    for(i = 1; i <= 8; i++)
    {
        corner = lower_left;

        corner[X] += ((i & 1) ? lengths[X] : 0.0);
        corner[Y] += ((i & 2) ? lengths[Y] : 0.0);
        corner[Z] += ((i & 4) ? lengths[Z] : 0.0);

        MInvTransPoint(corner, corner, trans);

        if(corner[X] < mins[X]) { mins[X] = corner[X]; }
        if(corner[X] > maxs[X]) { maxs[X] = corner[X]; }
        if(corner[Y] < mins[Y]) { mins[Y] = corner[Y]; }
        if(corner[Y] > maxs[Y]) { maxs[Y] = corner[Y]; }
        if(corner[Z] < mins[Z]) { mins[Z] = corner[Z]; }
        if(corner[Z] > maxs[Z]) { maxs[Z] = corner[Z]; }
    }

    // Clip bounding box at the largest allowed bounding box.
    if(mins[X] < -BOUND_HUGE / 2) { mins[X] = -BOUND_HUGE / 2; }
    if(mins[Y] < -BOUND_HUGE / 2) { mins[Y] = -BOUND_HUGE / 2; }
    if(mins[Z] < -BOUND_HUGE / 2) { mins[Z] = -BOUND_HUGE / 2; }
    if(maxs[X] >  BOUND_HUGE / 2) { maxs[X] =  BOUND_HUGE / 2; }
    if(maxs[Y] >  BOUND_HUGE / 2) { maxs[Y] =  BOUND_HUGE / 2; }
    if(maxs[Z] >  BOUND_HUGE / 2) { maxs[Z] =  BOUND_HUGE / 2; }

    Make_BBox_from_min_max(*bbox, mins, maxs);
}

// Create a bounding box hierarchy from a given list of finite and
// infinite elements. Each element consists of
//
// - an infinite flag
// - a bounding box enclosing the element
// - a pointer to the structure representing the element (e.g an object)
void Build_BBox_Tree(BBOX_TREE **Root, size_t numOfFiniteObjects, BBOX_TREE **&Finite, size_t numOfInfiniteObjects, BBOX_TREE **Infinite, size_t& maxfinitecount)
{
    ptrdiff_t low, high;
    BBOX_TREE *cd, *root;

    // This is a resonable guess at the number of finites needed.
    // This array will be reallocated as needed if it isn't.
    maxfinitecount = 2 * numOfFiniteObjects;

    // Now do a sort on the objects, with the end result being
    // a tree of objects sorted along the x, y, and z axes.
    if(numOfFiniteObjects > 0)
    {
        low = 0;
        high = numOfFiniteObjects;

        while (split_pass(Root, Finite, &numOfFiniteObjects, low, high, maxfinitecount))
        {
            low = high;
            high = numOfFiniteObjects;
        }

        // Move infinite objects in the first leaf of Root.
        if(numOfInfiniteObjects > 0)
        {
            root = *Root;
            root->Node = reinterpret_cast<BBOX_TREE **>(POV_REALLOC(root->Node, (root->Entries + 1) * sizeof(BBOX_TREE *), "composite"));
            std::memmove(&(root->Node[1]), &(root->Node[0]), root->Entries * sizeof(BBOX_TREE *));
            root->Entries++;
            cd = create_bbox_node(numOfInfiniteObjects);
            for(size_t i = 0; i < numOfInfiniteObjects; i++)
                cd->Node[i] = Infinite[i];

            calc_bbox(&(cd->BBox), Infinite, 0, numOfInfiniteObjects);
            root->Node[0] = cd;
            calc_bbox(&(root->BBox), root->Node, 0, root->Entries);

            // Root and first node are infinite.
            root->Infinite = true;
            root->Node[0]->Infinite = true;
        }
    }
    else
    {
        // There are no finite objects and no Root was created.
        // Create it now and put all infinite objects into it.

        if(numOfInfiniteObjects > 0)
        {
            cd = create_bbox_node(numOfInfiniteObjects);
            for(size_t i = 0; i < numOfInfiniteObjects; i++)
                cd->Node[i] = Infinite[i];
            calc_bbox(&(cd->BBox), Infinite, 0, numOfInfiniteObjects);
            *Root = cd;
            (*Root)->Infinite = true;
        }
    }
}

void Build_Bounding_Slabs(BBOX_TREE **Root, vector<ObjectPtr>& objects, unsigned int& numberOfFiniteObjects, unsigned int& numberOfInfiniteObjects, unsigned int& numberOfLightSources)
{
    ptrdiff_t iFinite, iInfinite;
    BBOX_TREE **Finite, **Infinite;
    ObjectPtr Temp;
    size_t maxfinitecount = 0;

    // Count frame level and infinite objects.
    numberOfFiniteObjects = numberOfInfiniteObjects = numberOfLightSources = 0;

    for(vector<ObjectPtr>::iterator i(objects.begin()); i != objects.end(); i++)
    {
        if((*i)->Type & LIGHT_SOURCE_OBJECT)
        {
            if((reinterpret_cast<LightSource *>(*i))->children.size() > 0)
            {
                Temp = (reinterpret_cast<LightSource *>(*i))->children[0];
                numberOfLightSources++;
            }
            else
                Temp = nullptr;
        }
        else
            Temp = (*i);

        if (Temp != nullptr)
        {
            if(Test_Flag(Temp, INFINITE_FLAG))
                numberOfInfiniteObjects++;
            else
                numberOfFiniteObjects++;
        }
    }

    // If bounding boxes aren't used we can return.
    if(numberOfFiniteObjects + numberOfInfiniteObjects < 1)
        return;

    // This is a reasonable guess at the number of finites needed.
    // This array will be reallocated as needed if it isn't.
    maxfinitecount = 2 * numberOfFiniteObjects;

    // Now allocate an array to hold references to these finites and
    // any new composite objects we may generate.
    Finite = Infinite = nullptr;

    if(numberOfFiniteObjects > 0)
        Finite = new BBOX_TREE* [maxfinitecount];

    // Create array to hold pointers to infinite objects.
    if(numberOfInfiniteObjects > 0)
        Infinite = new BBOX_TREE* [numberOfInfiniteObjects];

    // Init lists.
    for(int i = 0; i < numberOfFiniteObjects; i++)
        Finite[i] = create_bbox_node(0);

    for(int i = 0; i < numberOfInfiniteObjects; i++)
        Infinite[i] = create_bbox_node(0);

    // Set up finite and infinite object lists.
    iFinite = iInfinite = 0;

    for(vector<ObjectPtr>::iterator i(objects.begin()); i != objects.end(); i++)
    {
        if((*i)->Type & LIGHT_SOURCE_OBJECT)
        {
            if((reinterpret_cast<LightSource *>(*i))->children.size() > 0)
                Temp = (reinterpret_cast<LightSource *>(*i))->children[0];
            else
                Temp = nullptr;
        }
        else
            Temp = (*i);

        if (Temp != nullptr)
        {
            // Add object to the appropriate list.
            if(Test_Flag(Temp, INFINITE_FLAG))
            {
                Infinite[iInfinite]->Infinite = true;
                Infinite[iInfinite]->BBox     = Temp->BBox;
                Infinite[iInfinite]->Node     = reinterpret_cast<BBOX_TREE **>(Temp);

                iInfinite++;
            }
            else
            {
                Finite[iFinite]->BBox = Temp->BBox;
                Finite[iFinite]->Node = reinterpret_cast<BBOX_TREE **>(Temp);

                iFinite++;
            }
        }
    }

    // Now build the bounding box tree.
    Build_BBox_Tree(Root, numberOfFiniteObjects, Finite, numberOfInfiniteObjects, Infinite, maxfinitecount);

    // Get rid of the Finite and Infinite arrays and just use Root.
    if (Finite != nullptr)
        delete[] Finite;

    if (Infinite != nullptr)
        delete[] Infinite;
}

bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread)
{
    int i, found;
    DBL Depth;
    const BBOX_TREE *Node;
    Intersection New_Intersection;

    // Create the direction vectors for this ray.
    Rayinfo rayinfo(ray);

    // Start with an empty priority queue.
    pqueue.Clear();
    New_Intersection.Object = nullptr;
    found = false;

    // Check top node.
    Check_And_Enqueue(pqueue, Root, &Root->BBox, &rayinfo, Thread->Stats());

    // Check elements in the priority queue.
    while(!pqueue.IsEmpty())
    {
        pqueue.RemoveMin(Depth, Node);

        // If current intersection is larger than the best intersection found
        // so far our task is finished, because all other bounding boxes in
        // the priority queue are further away.
        if(Depth > Best_Intersection->Depth)
            break;

        // Check current node.
        if(Node->Entries)
        {
            // This is a node containing leaves to be checked.
            for (i = 0; i < Node->Entries; i++)
                Check_And_Enqueue(pqueue, Node->Node[i], &Node->Node[i]->BBox, &rayinfo, Thread->Stats(), Best_Intersection->Depth);
        }
        else
        {
            // This is a leaf so test contained object.
            if(Find_Intersection(&New_Intersection, reinterpret_cast<ObjectPtr>(Node->Node), ray, Thread))
            {
                if(New_Intersection.Depth < Best_Intersection->Depth)
                {
                    *Best_Intersection = New_Intersection;
                    found = true;
                }
            }
        }
    }

    return (found);
}

bool Intersect_BBox_Tree(BBoxPriorityQueue& pqueue, const BBOX_TREE *Root, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread)
{
    int i, found;
    DBL Depth;
    const BBOX_TREE *Node;
    Intersection New_Intersection;

    // Create the direction vectors for this ray.
    Rayinfo rayinfo(ray);

    // Start with an empty priority queue.
    pqueue.Clear();
    New_Intersection.Object = nullptr;
    found = false;

    // Check top node.
    Check_And_Enqueue(pqueue, Root, &Root->BBox, &rayinfo, Thread->Stats());

    // Check elements in the priority queue.
    while(!pqueue.IsEmpty())
    {
        pqueue.RemoveMin(Depth, Node);

        // If current intersection is larger than the best intersection found
        // so far our task is finished, because all other bounding boxes in
        // the priority queue are further away.
        if(Depth > Best_Intersection->Depth)
            break;

        // Check current node.
        if(Node->Entries)
        {
            // This is a node containing leaves to be checked.
            for (i = 0; i < Node->Entries; i++)
                Check_And_Enqueue(pqueue, Node->Node[i], &Node->Node[i]->BBox, &rayinfo, Thread->Stats(), Best_Intersection->Depth);
        }
        else
        {
            if(precondition(ray, reinterpret_cast<ObjectPtr>(Node->Node), 0.0) == true)
            {
                // This is a leaf so test contained object.
                if(Find_Intersection(&New_Intersection, reinterpret_cast<ObjectPtr>(Node->Node), ray, postcondition, Thread))
                {
                    if(New_Intersection.Depth < Best_Intersection->Depth)
                    {
                        *Best_Intersection = New_Intersection;
                        found = true;
                    }
                }
            }
        }
    }

    return (found);
}

static void Set_Flat_Lane(FlatBBoxBlock& b, int k, const BoundingBox& box, bool infinite)
{
    for (int d = X; d <= Z; ++d)
    {
        if (infinite)
        {
            b.lo[d][k] = -FLAT_BBOX_FAR;
            b.hi[d][k] = FLAT_BBOX_FAR;
        }
        else
        {
            // Padded outward so single-precision rounding never rejects a box the old test accepted.
            const float lo = box.lowerLeft[d], hi = box.lowerLeft[d] + box.size[d];
            const float pad = 1.0e-6f * (std::fabs(lo) + std::fabs(hi)) + 1.0e-20f;
            b.lo[d][k] = lo - pad;
            b.hi[d][k] = hi + pad;
        }
    }
}

// A pointer tree as the flattener reads it.
struct PointerTreeSource final
{
    typedef const BBOX_TREE *Ref;
    vector<const void *>& leaves;

    bool Leaf(Ref r) const { return r->Entries == 0; }
    int Entries(Ref r) const { return r->Entries; }
    Ref Child(Ref r, int i) const { return r->Node[i]; }
    bool Infinite(Ref r) const { return r->Infinite; }
    BoundingBox Box(Ref r) const { return r->BBox; }
    std::int32_t LeafId(Ref r) const
    {
        leaves.push_back(reinterpret_cast<const void *>(r->Node));
        return std::int32_t(leaves.size() - 1);
    }
};

// The same tree built straight from leaf boxes: nodes in an array, leaf k referenced as -1-k.
struct BoxTreeSource final
{
    typedef std::int32_t Ref;
    struct Node final
    {
        BoundingBox box;
        std::uint32_t first, count;
    };
    vector<Node> nodes;
    vector<Ref> kids;
    const FlatLeafBoxFn& leafBox;

    explicit BoxTreeSource(const FlatLeafBoxFn& fn) : leafBox(fn) {}
    bool Leaf(Ref r) const { return r < 0; }
    int Entries(Ref r) const { return int(nodes[r].count); }
    Ref Child(Ref r, int i) const { return kids[nodes[r].first + i]; }
    bool Infinite(Ref) const { return false; }
    BoundingBox Box(Ref r) const
    {
        if (r >= 0)
            return nodes[r].box;
        BoundingBox b;
        leafBox(size_t(-1 - r), b);
        return b;
    }
    std::int32_t LeafId(Ref r) const { return -1 - r; }
};

// Pull up the grandchildren of the largest child nodes while they fit, so blocks run full.
template<typename Src>
static void Flat_Kids(const Src& src, typename Src::Ref node, vector<typename Src::Ref>& kids)
{
    kids.clear();
    for (int i = 0; i < src.Entries(node); ++i)
        kids.push_back(src.Child(node, i));
    while (kids.size() < size_t(FLAT_BBOX_WIDTH))
    {
        int pick = -1;
        BBoxScalar pickArea = -1.0f;
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const typename Src::Ref c = kids[i];
            if (src.Leaf(c) || src.Infinite(c) || (kids.size() - 1 + src.Entries(c) > size_t(FLAT_BBOX_WIDTH)))
                continue;
            const BBoxVector3d s = src.Box(c).size;
            const BBoxScalar area = s[X] * s[Y] + s[Y] * s[Z] + s[Z] * s[X];
            if (area > pickArea)
            {
                pickArea = area;
                pick = int(i);
            }
        }
        if (pick < 0)
            break;
        const typename Src::Ref c = kids[pick];
        kids.erase(kids.begin() + pick);
        for (int i = 0; i < src.Entries(c); ++i)
            kids.push_back(src.Child(c, i));
    }
}

template<typename Src>
static size_t Count_Flat_Blocks(const Src& src, typename Src::Ref node)
{
    if (src.Leaf(node))
        return 0;
    vector<typename Src::Ref> kids;
    Flat_Kids(src, node, kids);
    size_t n = (kids.size() + FLAT_BBOX_WIDTH - 1) / FLAT_BBOX_WIDTH;
    for (const typename Src::Ref k : kids)
        n += Count_Flat_Blocks(src, k);
    return n;
}

static void Fill_Flat_Block(FlatBBoxBlock& b, const BoundingBox *boxes, const bool *infinite, int count, bool more)
{
    for (int d = X; d <= Z; ++d)
        for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
        {
            b.lo[d][k] = FLAT_BBOX_FAR;
            b.hi[d][k] = -FLAT_BBOX_FAR;
        }
    for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
        b.child[k] = 0;
    b.count = count;
    b.more = more;
    for (int k = 0; k < count; ++k)
        Set_Flat_Lane(b, k, boxes[k], infinite[k]);
}

static void Fill_Flat_Block(FlatQBBoxBlock& b, const BoundingBox *boxes, const bool *infinite, int count, bool more)
{
    const float QMAX = 65535.0f;
    FlatBBoxBlock f;
    Fill_Flat_Block(f, boxes, infinite, count, more);
    for (int d = X; d <= Z; ++d)
    {
        float lo = FLAT_BBOX_FAR, hi = -FLAT_BBOX_FAR;
        for (int k = 0; k < count; ++k)
        {
            POV_ASSERT(!infinite[k]);
            lo = std::min(lo, f.lo[d][k]);
            hi = std::max(hi, f.hi[d][k]);
        }
        // A step of margin at both grid ends and on every lane, since decoding cancels terms of the block's size.
        float scale = (hi - lo) / (QMAX - 2.0f);
        while ((lo - scale) + (QMAX - 1.0f) * scale < hi)
            scale = std::nextafter(scale, FLAT_BBOX_FAR);
        lo -= scale;
        b.origin[d] = lo;
        b.scale[d] = scale;
        for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
        {
            b.lo[d][k] = b.hi[d][k] = 0;
            if ((k >= count) || !(scale > 0.0f))
                continue;
            float q = std::min(QMAX, std::max(0.0f, std::floor((f.lo[d][k] - lo) / scale)));
            while ((q > 0.0f) && (lo + q * scale > f.lo[d][k]))
                q -= 1.0f;
            float r = std::min(QMAX, std::max(0.0f, std::ceil((f.hi[d][k] - lo) / scale)));
            while ((r < QMAX) && (lo + r * scale < f.hi[d][k]))
                r += 1.0f;
            q = std::max(0.0f, q - 1.0f);
            r = std::min(QMAX, r + 1.0f);
            b.lo[d][k] = std::uint16_t(q);
            b.hi[d][k] = std::uint16_t(r);
        }
    }
    for (int k = 0; k < FLAT_BBOX_WIDTH; ++k)
        b.child[k] = 0;
    b.count = std::uint16_t(count);
    b.more = std::uint16_t(more);
}

template<typename Block, typename Src>
static std::int32_t Flatten_Node(FlatBBoxTreeOf<Block>& tree, const Src& src, typename Src::Ref node)
{
    if (src.Leaf(node))
        return -1 - src.LeafId(node);

    vector<typename Src::Ref> kids;
    Flat_Kids(src, node, kids);
    const std::int32_t first = std::int32_t(tree.blocks.size());
    const int count = int(kids.size()), nblocks = (count + FLAT_BBOX_WIDTH - 1) / FLAT_BBOX_WIDTH;
    for (int i = 0; i < nblocks; ++i)
    {
        BoundingBox boxes[FLAT_BBOX_WIDTH];
        bool infinite[FLAT_BBOX_WIDTH];
        const int n = std::min(FLAT_BBOX_WIDTH, count - i * FLAT_BBOX_WIDTH);
        for (int k = 0; k < n; ++k)
        {
            boxes[k] = src.Box(kids[i * FLAT_BBOX_WIDTH + k]);
            infinite[k] = src.Infinite(kids[i * FLAT_BBOX_WIDTH + k]);
        }
        tree.blocks.emplace_back();
        Fill_Flat_Block(tree.blocks.back(), boxes, infinite, n, i + 1 < nblocks);
    }
    for (int i = 0; i < count; ++i)
    {
        const std::int32_t ref = Flatten_Node(tree, src, kids[i]);
        tree.blocks[first + i / FLAT_BBOX_WIDTH].child[i % FLAT_BBOX_WIDTH] = ref;
    }
    return first;
}

template<typename Block, typename Src>
static FlatBBoxTreeOf<Block> *Flatten(const Src& src, typename Src::Ref root, FlatBBoxTreeOf<Block> *tree)
{
    const BoundingBox box = src.Box(root);
    const bool infinite = src.Infinite(root);
    tree->blocks.reserve(1 + Count_Flat_Blocks(src, root));
    tree->blocks.emplace_back();
    Fill_Flat_Block(tree->blocks[0], &box, &infinite, 1, false);
    const std::int32_t ref = Flatten_Node(*tree, src, root);
    tree->blocks[0].child[0] = ref;
    tree->leaves.shrink_to_fit();
    return tree;
}

FlatBBoxTree *Build_Flat_BBox_Tree(const BBOX_TREE *Root)
{
    if (Root == nullptr)
        return nullptr;

    FlatBBoxTree *tree = new FlatBBoxTree;
    return Flatten(PointerTreeSource{tree->leaves}, Root, tree);
}

static void Calc_BBox(BoundingBox& box, const BoundingBox *boxes, const std::uint32_t *idx, size_t size)
{
    Vector3d bmin(BOUND_HUGE), bmax(-BOUND_HUGE);
    for (size_t i = 0; i < size; ++i)
    {
        const BoundingBox& b = boxes[idx[i]];
        for (int d = X; d <= Z; ++d)
        {
            const DBL tmin = b.lowerLeft[d], tmax = tmin + b.size[d];
            bmin[d] = std::min(bmin[d], tmin);
            bmax[d] = std::max(bmax[d], tmax);
        }
    }
    Make_BBox_from_min_max(box, bmin, bmax);
}

FlatMeshBBoxTree *Build_Flat_BBox_Tree(size_t numLeaves, const FlatLeafBoxFn& leafBox)
{
    if (numLeaves == 0)
        return nullptr;

    BoxTreeSource src(leafBox);
    vector<BoundingBox> boxes(numLeaves), nextBoxes;
    vector<std::int32_t> refs(numLeaves), nextRefs;
    for (size_t i = 0; i < numLeaves; ++i)
    {
        leafBox(i, boxes[i]);
        refs[i] = -1 - std::int32_t(i);
    }
    for (bool more = true; more; )
    {
        more = Split_BBox_Pass(boxes.data(), boxes.size(), [&](const std::uint32_t *idx, size_t size) {
            BoxTreeSource::Node node;
            node.first = std::uint32_t(src.kids.size());
            node.count = std::uint32_t(size);
            for (size_t i = 0; i < size; ++i)
                src.kids.push_back(refs[idx[i]]);
            Calc_BBox(node.box, boxes.data(), idx, size);
            nextRefs.push_back(std::int32_t(src.nodes.size()));
            nextBoxes.push_back(node.box);
            src.nodes.push_back(node);
        });
        boxes.swap(nextBoxes);
        refs.swap(nextRefs);
        vector<BoundingBox>().swap(nextBoxes);
        vector<std::int32_t>().swap(nextRefs);
    }
    vector<BoundingBox>().swap(boxes);
    return Flatten(src, std::int32_t(src.nodes.size() - 1), new FlatMeshBBoxTree);
}

bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, TraceThreadData *Thread)
{
    Intersection New_Intersection;
    bool found = false;

    Traverse_Flat_BBox_Tree_Ordered(tree, ray, Best_Intersection->Depth, Thread->Stats(), [&](std::int32_t leaf) {
        if (Find_Intersection(&New_Intersection, reinterpret_cast<ObjectPtr>(const_cast<void *>(tree.leaves[leaf])), ray, Thread) &&
            (New_Intersection.Depth < Best_Intersection->Depth))
        {
            *Best_Intersection = New_Intersection;
            found = true;
        }
        return false;
    });
    return found;
}

bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, TraceThreadData *Thread)
{
    Intersection New_Intersection;
    bool found = false;

    Traverse_Flat_BBox_Tree_Ordered(tree, ray, Best_Intersection->Depth, Thread->Stats(), [&](std::int32_t leaf) {
        ObjectPtr object = reinterpret_cast<ObjectPtr>(const_cast<void *>(tree.leaves[leaf]));
        if (precondition(ray, object, 0.0) &&
            Find_Intersection(&New_Intersection, object, ray, postcondition, Thread) &&
            (New_Intersection.Depth < Best_Intersection->Depth))
        {
            *Best_Intersection = New_Intersection;
            found = true;
        }
        return false;
    });
    return found;
}

bool Intersect_Flat_BBox_Tree(const FlatBBoxTree& tree, const Ray& ray, Intersection *Best_Intersection, const RayObjectCondition& precondition, const RayObjectCondition& postcondition, const IntersectionStopCondition& stop, TraceThreadData *Thread)
{
    Intersection New_Intersection;
    bool found = false;

    Traverse_Flat_BBox_Tree(tree, ray, Best_Intersection->Depth, true, Thread->Stats(), [&](std::int32_t leaf) {
        ObjectPtr object = reinterpret_cast<ObjectPtr>(const_cast<void *>(tree.leaves[leaf]));
        if (precondition(ray, object, 0.0) &&
            Find_Intersection(&New_Intersection, object, ray, postcondition, Thread) &&
            (New_Intersection.Depth < Best_Intersection->Depth))
        {
            *Best_Intersection = New_Intersection;
            found = true;
            return stop(New_Intersection);
        }
        return false;
    });
    return found;
}

void Check_And_Enqueue(BBoxPriorityQueue& Queue, const BBOX_TREE *Node, const BoundingBox *BBox, const Rayinfo *rayinfo, RenderStatistics& Stats, DBL maxDepth)
{
    DBL dmin, dmax;

    if(Node->Infinite == false)
    {
        Stats[nChecked]++;

        // Test whether the bounding box is being hit.

        // The bounding box can be thought of as an intersection of three "slabs", by which we mean slices of 3D space
        // bounded by parallel axis-aligned planes; we have an "X slab" bounding the box in the X dimension, an
        // "Y slab", and a "Z slab".

        // With a few exceptions that we need to test for, any ray will intersect all three slabs, defining an interval
        // along the ray in which the ray is inside the slab.

        // Where the intervals for the individual slabs overlap, the ray is inside bounding box.

        // We proceed one dimension at a time.

        // These will keep track of the overlap between the intervals.
        dmin = -BOUND_HUGE;
        dmax =  BOUND_HUGE;

        for (int dim = X; dim <= Z; ++dim)
        {
            if(rayinfo->nonzero[dim])
            {
                // These will hold the distance to the near and far plane, respectively, for this slab.
                DBL tmin, tmax;

                if (rayinfo->positive[dim])
                {
                    // Far plane is "upper" plane, near plane is the "lower" one.
                    tmax = (BBox->lowerLeft[dim] + BBox->size[dim] - rayinfo->origin[dim]) * rayinfo->invDirection[dim];
                    if(tmax < EPSILON)
                        // The far plane is (at least almost) behind the observer,
                        // so the ray is heading away from the box and can't possibly intersect it.
                        return;
                    tmin = (BBox->lowerLeft[dim] - rayinfo->origin[dim]) * rayinfo->invDirection[dim];
                }
                else
                {
                    // Far plane is "lower" plane, near plane is the "upper" one.
                    tmax = (BBox->lowerLeft[dim] - rayinfo->origin[dim]) * rayinfo->invDirection[dim];
                    if(tmax < EPSILON)
                        // The far plane is (at least almost) behind the observer,
                        // so the ray is heading away from the box and can't possibly intersect it.
                        return;
                    tmin = (BBox->lowerLeft[dim] + BBox->size[dim] - rayinfo->origin[dim]) * rayinfo->invDirection[dim];
                }

                // The next portion of code is essentially the same as the following
                // (presuming dmin <= dmax initially), with a lot of shortcuts to bail out early:
                //
                //  if (tmax < dmax) dmax = tmax;   // update the overlap lower bound
                //  if (tmin > dmin) dmin = tmin;   // update the overlap upper bound
                //  if (dmin > dmax) return;        // detect whether there is no overlap

                if (tmax < dmax)
                {
                    if (tmin > dmin)
                    {
                        if(tmin > tmax)
                            return;
                        dmin = tmin;
                    }
                    else
                    {
                        if(dmin > tmax)
                            return;
                    }
                    dmax = tmax;
                }
                else
                {
                    if(tmin > dmin)
                    {
                        if(tmin > dmax)
                            return;
                        dmin = tmin;
                    }
                }
            }
            else
            {
                // Special case: The ray runs parallel to this slab; there ray is either entirely inside the slab,
                // or it is entirely outside; we can easily check this by testing the ray origin.

                if (!IsInRange (rayinfo->origin[dim], BBox->lowerLeft[dim], BBox->lowerLeft[dim] + BBox->size[dim]))
                    // The ray is entirely outside the slab, so it can't possibly hit the bounding box.
                    return;

                // The ray is entirely inside the slab, so this slab has no effect on the end result.
            }
        }

        // If we've made it through to here, the ray does hit the box.
        if (dmin > maxDepth)
            return;

        Stats[nEnqueued]++;
    }
    else
        // Set intersection depth to -Max_Distance.
        dmin = -MAX_DISTANCE;

    Queue.Insert (dmin, Node);
}

BBOX_TREE *create_bbox_node(int size)
{
    BBOX_TREE *New;

    New = reinterpret_cast<BBOX_TREE *>(POV_MALLOC(sizeof(BBOX_TREE), "bounding box node"));

    New->Infinite = false;
    New->Entries = size;

    Make_BBox(New->BBox, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    if(size)
        New->Node = reinterpret_cast<BBOX_TREE **>(POV_MALLOC(size*sizeof(BBOX_TREE *), "bounding box node"));
    else
        New->Node = nullptr;

    return (New);
}

void calc_bbox(BoundingBox *BBox, BBOX_TREE **Finite, ptrdiff_t first, ptrdiff_t last)
{
    ptrdiff_t i;
    DBL tmin, tmax;
    Vector3d bmin, bmax;
    BoundingBox *bbox;

    bmin = Vector3d(BOUND_HUGE);
    bmax = Vector3d(-BOUND_HUGE);

    for(i = first; i < last; i++)
    {
        bbox = &(Finite[i]->BBox);

        tmin = bbox->lowerLeft[X];
        tmax = tmin + bbox->size[X];

        if(tmin < bmin[X]) { bmin[X] = tmin; }
        if(tmax > bmax[X]) { bmax[X] = tmax; }

        tmin = bbox->lowerLeft[Y];
        tmax = tmin + bbox->size[Y];

        if(tmin < bmin[Y]) { bmin[Y] = tmin; }
        if(tmax > bmax[Y]) { bmax[Y] = tmax; }

        tmin = bbox->lowerLeft[Z];
        tmax = tmin + bbox->size[Z];

        if(tmin < bmin[Z]) { bmin[Z] = tmin; }
        if(tmax > bmax[Z]) { bmax[Z] = tmax; }
    }

    Make_BBox_from_min_max(*BBox, bmin, bmax);
}

static inline BBoxScalar half_area(const BBoxVector3d& lo, const BBoxVector3d& hi)
{
    const BBoxVector3d len = hi - lo;
    return len[X] * (len[Y] + len[Z]) + len[Y] * len[Z];
}

static inline void grow(BBoxVector3d& lo, BBoxVector3d& hi, const BoundingBox& b)
{
    for (int d = X; d <= Z; ++d)
    {
        lo[d] = std::min(lo[d], b.lowerLeft[d]);
        hi[d] = std::max(hi[d], BBoxScalar(b.lowerLeft[d] + b.size[d]));
    }
}

// One pass of the bottom-up build: the pass's boxes sorted once per axis, the three orders kept by stable
// partition as the range is split, so every split is the exact surface-area sweep without re-sorting.
struct SplitPass final
{
    const BoundingBox *boxes;
    vector<std::uint32_t> order[3];
    vector<std::uint8_t> left;
    vector<std::uint32_t> scratch;
    vector<BBoxScalar> areaRight;
    const BBoxGroupFn& emit;

    SplitPass(const BoundingBox *b, const BBoxGroupFn& e) : boxes(b), emit(e) {}

    bool split(ptrdiff_t s, ptrdiff_t e)
    {
        const ptrdiff_t size = e - s;
        int bestAxis = -1;
        ptrdiff_t bestCount = 0;

        // Don't bother to do any further examinations if the BUNCHING_FACTOR is reached.
        if (size > BUNCHING_FACTOR)
        {
            BBoxScalar best = 0.0f;
            for (int axis = X; axis <= Z; ++axis)
            {
                const std::uint32_t *ord = order[axis].data();
                BBoxVector3d lo(BOUND_HUGE), hi(-BOUND_HUGE);
                for (ptrdiff_t i = e - 1; i >= s; --i)
                {
                    grow(lo, hi, boxes[ord[i]]);
                    areaRight[i - s] = half_area(lo, hi);
                }
                if (axis == X)
                    best = areaRight[0] * float(size - 3); // estimated cost of _not_ subdividing
                lo = BBoxVector3d(BOUND_HUGE);
                hi = BBoxVector3d(-BOUND_HUGE);
                for (ptrdiff_t i = s; i < e - 1; ++i)
                {
                    grow(lo, hi, boxes[ord[i]]);
                    const ptrdiff_t n = i - s + 1;
                    const BBoxScalar cost = float(n) * half_area(lo, hi) + float(size - n) * areaRight[n];
                    if (cost < best)
                    {
                        best = cost;
                        bestAxis = axis;
                        bestCount = n;
                    }
                }
            }
        }

        // Stop splitting if splitting stops being effective.
        if (bestAxis < 0)
        {
            emit(order[X].data() + s, size_t(size));
            return false;
        }

        for (ptrdiff_t i = s; i < e; ++i)
            left[order[bestAxis][i]] = (i < s + bestCount);
        for (int axis = X; axis <= Z; ++axis)
        {
            if (axis == bestAxis)
                continue;
            std::uint32_t *ord = order[axis].data();
            ptrdiff_t l = s, r = 0;
            for (ptrdiff_t i = s; i < e; ++i)
            {
                if (left[ord[i]])
                    ord[l++] = ord[i];
                else
                    scratch[r++] = ord[i];
            }
            std::copy(scratch.begin(), scratch.begin() + r, ord + l);
        }
        split(s, s + bestCount);
        split(s + bestCount, e);
        return true;
    }
};

bool Split_BBox_Pass(const BoundingBox *boxes, size_t size, const BBoxGroupFn& emit)
{
    if (size == 0)
        return false;

    SplitPass pass(boxes, emit);
    {
        vector<std::pair<DBL, std::uint32_t>> keyed(size);
        for (int axis = X; axis <= Z; ++axis)
        {
            for (size_t i = 0; i < size; ++i)
                keyed[i] = std::make_pair(2.0 * boxes[i].lowerLeft[axis] + boxes[i].size[axis], std::uint32_t(i));
            std::sort(keyed.begin(), keyed.end());
            pass.order[axis].resize(size);
            for (size_t i = 0; i < size; ++i)
                pass.order[axis][i] = keyed[i].second;
        }
    }
    pass.left.resize(size);
    pass.scratch.resize(size);
    pass.areaRight.resize(size);
    return pass.split(0, ptrdiff_t(size));
}

bool split_pass(BBOX_TREE **Root, BBOX_TREE **&Finite, size_t *numOfFiniteObjects, ptrdiff_t first, ptrdiff_t last, size_t& maxfinitecount)
{
    if (last <= first)
        return false;

    const vector<BBOX_TREE *> items(Finite + first, Finite + last);
    vector<BoundingBox> boxes(items.size());
    for (size_t i = 0; i < items.size(); ++i)
        boxes[i] = items[i]->BBox;
    return Split_BBox_Pass(boxes.data(), boxes.size(), [&](const std::uint32_t *idx, size_t size) {
        BBOX_TREE *cd = create_bbox_node(int(size));
        for (size_t i = 0; i < size; ++i)
            cd->Node[i] = items[idx[i]];
        calc_bbox(&(cd->BBox), cd->Node, 0, size);
        *Root = cd;
        if (*numOfFiniteObjects >= maxfinitecount)
        {
            // Prim array overrun, increase array by 50%.
            maxfinitecount = 1.5 * maxfinitecount;
            Finite = reinterpret_cast<BBOX_TREE **>(POV_REALLOC(Finite, maxfinitecount * sizeof(BBOX_TREE *), "bounding boxes"));
        }
        Finite[*numOfFiniteObjects] = cd;
        (*numOfFiniteObjects)++;
    });
}

}
// end of namespace pov
