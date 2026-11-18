// Contract of an object's surface boxes: pieces in the structure's order, each piece's hits inside its own boxes, every hit
// attributed to one piece and leaf, the same in copies and at any scale, and queries answered from the child trees.
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/box.h"
#include "core/shape/csg.h"
#include "core/shape/plane.h"
#include "core/shape/sphere.h"
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

using namespace pov;

static Sphere *Ball(const Vector3d& centre, double radius)
{
    Sphere *s = new Sphere();
    s->Center = centre;
    s->Radius = radius;
    s->Compute_BBox();
    return s;
}

static Box *Brick(const Vector3d& low, const Vector3d& high)
{
    Box *b = new Box();
    b->bounds[0] = low;
    b->bounds[1] = high;
    b->Compute_BBox();
    return b;
}

template <typename T>
static T *Group(T *csg, std::vector<ObjectPtr> children)
{
    for (ObjectPtr child : children)
    {
        child->Type |= IS_CHILD_OBJECT;
        csg->children.push_back(child);
    }
    csg->Compute_BBox();
    return csg;
}

static void Prepare(ObjectPtr object)
{
    std::unordered_set<ConstObjectPtr> visited;
    unsigned built = 0;
    Prepare_CSG_Child_Trees(object, CSG_Child_Tree_Minimum(), visited, built);
}

static std::vector<SurfaceBox> All(ConstObjectPtr object)
{
    std::vector<SurfaceBox> out;
    Surface_Boxes(object, Vector3d(-HUGE_VAL), Vector3d(HUGE_VAL), out);
    return out;
}

static bool Same(const Vector3d& a, const Vector3d& b)
{
    return (a.x() == b.x()) && (a.y() == b.y()) && (a.z() == b.z());
}

static bool Same(const BoundingBox& a, const BoundingBox& b)
{
    return Same(Vector3d(a.lowerLeft), Vector3d(b.lowerLeft)) && Same(Vector3d(a.size), Vector3d(b.size));
}

/// Whether the box holds a hit up to the rounding of the hit itself, as Ray::MatchesSurfacePoint allows for (ray.h).
static bool Holds(const BoundingBox& box, const Vector3d& p, const Vector3d& origin, const Vector3d& travel)
{
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, box);
    for (int a = X; a <= Z; ++a)
    {
        const double rounding = 16.0 * std::numeric_limits<double>::epsilon() *
                                std::max(std::fabs(p[a]), std::max(std::fabs(origin[a]), std::fabs(travel[a])));
        if ((p[a] < low[a] - rounding) || (p[a] > high[a] + rounding))
            return false;
    }
    return true;
}

/// Fires rays into its pieces' boxes: every hit is surface of one piece and leaf, inside that piece's own boxes; returns the hits.
static unsigned Attribution(ObjectPtr object, TraceThreadData& thread, unsigned rays)
{
    const std::vector<SurfaceBox> boxes = All(object);
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, object->BBox);
    const Vector3d centre = (low + high) * 0.5;
    const double reach = (high - low).length() + 1.0;
    std::mt19937_64 random(7);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    TraceTicket ticket(8, 0.0);
    unsigned hits = 0;
    for (unsigned r = 0; r < rays; ++r)
    {
        Vector3d from(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5);
        from = centre + from.normalized() * reach;
        // aim into one piece's box, so small pieces far apart are hit too
        Vector3d a, b;
        Make_min_max_from_BBox(a, b, boxes[r % boxes.size()].box);
        const Vector3d to(a.x() + unit(random) * (b.x() - a.x()), a.y() + unit(random) * (b.y() - a.y()), a.z() + unit(random) * (b.z() - a.z()));
        Ray ray(ticket, from, (to - from).normalized());
        Intersection hit;
        if (!Find_Intersection(&hit, object, ray, &thread))
            continue;
        ++hits;
        std::uint32_t piece = ~0u, leaf = ~0u;
        const bool surface = Surface_Part(object, hit, piece, leaf);
        assert(surface);
        assert(piece < object->Surface_Pieces());
        assert(object->Surface_Leaf(leaf) == hit.Object);
        bool inOwn = false;
        for (const SurfaceBox& b : boxes)
            inOwn = inOwn || ((b.piece == piece) && Holds(b.box, hit.IPoint, from, ray.Direction * hit.Depth));
        if (!inOwn)
            for (const SurfaceBox& b : boxes)
                if (b.piece == piece)
                    std::fprintf(stderr, "hit %.17g %.17g %.17g of %s outside piece %u box %.9g %.9g %.9g + %.9g %.9g %.9g\n",
                                 hit.IPoint.x(), hit.IPoint.y(), hit.IPoint.z(), ShapeName(hit.Object), piece,
                                 double(b.box.lowerLeft[X]), double(b.box.lowerLeft[Y]), double(b.box.lowerLeft[Z]),
                                 double(b.box.size[X]), double(b.box.size[Y]), double(b.box.size[Z]));
        assert(inOwn);
    }
    return hits;
}

/// Instructions this thread runs in `work`, or 0 where counters are not available.
template <typename Work>
static long long Instructions(const Work& work)
{
    perf_event_attr attr = {};
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    const int fd = int(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
    if (fd < 0)
    {
        work();
        return 0;
    }
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    work();
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    long long count = 0;
    if (read(fd, &count, sizeof(count)) != sizeof(count))
        count = 0;
    close(fd);
    return count;
}

/// A structure with every kind of piece: a sphere, a brick row in a union, a merge, an intersection and a difference.
static CSGUnion *Scene()
{
    std::vector<ObjectPtr> row;
    for (int k = 0; k < 10; ++k)
        row.push_back(Brick(Vector3d(k * 2.0, 0, 0), Vector3d(k * 2.0 + 1, 1, 1)));
    CSGIntersection *lens = Group(new CSGIntersection(false), {Ball(Vector3d(0, -3, 5), 3.2), Ball(Vector3d(0, 3, 5), 3.2)});
    CSGIntersection *bitten = Group(new CSGIntersection(true), {Brick(Vector3d(4, 0, 4), Vector3d(6, 2, 6)),
                                                                Ball(Vector3d(6, 2, 6), 1.0)->Invert()});
    CSGMerge *blob = Group(new CSGMerge(), {Ball(Vector3d(-4, 0, 0), 1), Ball(Vector3d(-3.5, 0, 0), 1)});
    return Group(new CSGUnion(), {Ball(Vector3d(0, 4, 0), 0.5), Group(new CSGUnion(), row), blob, lens, bitten});
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Initialize_Noise();
    auto sceneData = std::make_shared<SceneData>();
    TraceThreadData thread(sceneData, 1, false);

    // a primitive is one piece, one leaf, its own box; a sphere also gives its sphere
    Sphere *ball = Ball(Vector3d(1, 2, 3), 0.5);
    std::vector<SurfaceBox> boxes = All(ball);
    assert((boxes.size() == 1) && (boxes[0].piece == 0) && (boxes[0].sphereRadius == 0.5) && Same(boxes[0].sphereCentre, Vector3d(1, 2, 3)));
    assert((ball->Surface_Pieces() == 1) && (ball->Surface_Leaves() == 1) && (ball->Surface_Leaf(0) == ball));
    Box *brick = Brick(Vector3d(0.0), Vector3d(1));
    assert((All(brick).size() == 1) && (All(brick)[0].sphereRadius == 0.0));

    // two tiny spheres far apart: two boxes, not the 200-unit box that holds both
    CSGUnion *pair = Group(new CSGUnion(), {Ball(Vector3d(-100, 0, 0), 0.1), Ball(Vector3d(100, 0, 0), 0.1)});
    boxes = All(pair);
    assert((boxes.size() == 2) && (boxes[0].piece == 0) && (boxes[1].piece == 1));
    for (const SurfaceBox& b : boxes)
        assert(b.box.size[X] < 0.21f);
    assert(pair->BBox.size[X] > 200.0f);
    std::printf("two far spheres: %zu boxes of %.3g, union box %.4g wide\n", boxes.size(), double(boxes[0].box.size[X]), double(pair->BBox.size[X]));

    // nested unions number depth first; an intersection, a difference and a merge's members: a merge's members are pieces,
    // an intersection or difference is one piece of one box, its shapes its leaves
    CSGUnion *scene = Scene();
    assert(scene->Surface_Pieces() == 1 + 10 + 2 + 1 + 1);
    assert(scene->Surface_Leaves() == 1 + 10 + 2 + 2 + 2);
    boxes = All(scene);
    assert(boxes.size() == 15);
    for (std::uint32_t k = 0; k < boxes.size(); ++k)
        assert(boxes[k].piece == k);
    const CSG *lens = static_cast<const CSG *>(scene->children[3]);
    assert(Same(boxes[13].box, lens->BBox));
    assert(scene->Surface_Leaf(13) == lens->children[0]);
    assert(scene->Surface_Leaf(17) == nullptr);
    std::printf("structure: %u pieces, %u leaves\n", scene->Surface_Pieces(), scene->Surface_Leaves());

    // a clipped union keeps its members as pieces; its hits are only those the clip keeps
    CSGUnion *clipped = Group(new CSGUnion(), {Ball(Vector3d(0.0), 1), Ball(Vector3d(1.5, 0, 0), 1)});
    Plane *cut = new Plane();
    cut->Normal_Vector = Vector3d(0, 1, 0);
    cut->Distance = 0;
    cut->Compute_BBox();
    clipped->Clip.push_back(cut);
    assert(All(clipped).size() == 2);

    // every hit belongs to exactly one piece and lands in that piece's own boxes; unprepared, then with child trees
    const unsigned rays = 4000;
    for (bool prepared : {false, true})
    {
        if (prepared)
            for (ObjectPtr o : std::vector<ObjectPtr>{pair, scene, clipped})
                Prepare(o);
        unsigned hits = 0;
        for (ObjectPtr o : std::vector<ObjectPtr>{scene, clipped, pair})
        {
            const unsigned found = Attribution(o, thread, rays);
            assert(found > rays / 4);
            hits += found;
        }
        std::printf("attribution (%s): %u hits, each in its own piece's box\n", prepared ? "child trees" : "no trees", hits);
    }

    // a portal's window is no surface
    Intersection window;
    window.Object = ball;
    window.IPoint = Vector3d(1, 2, 3.5);
    Sphere *mouth = Ball(Vector3d(0.0), 1);
    Set_Flag(mouth, PORTAL_FLAG);
    window.Csg = mouth;
    std::uint32_t piece, leaf;
    const bool windowIsSurface = Surface_Part(ball, window, piece, leaf);
    assert(!windowIsSurface);

    // a copy numbers and boxes its pieces as the original does
    ObjectPtr copy = Copy_Object(scene);
    std::vector<SurfaceBox> copied = All(copy);
    assert(copied.size() == boxes.size());
    for (size_t k = 0; k < boxes.size(); ++k)
        assert((copied[k].piece == boxes[k].piece) && Same(copied[k].box, boxes[k].box));

    // the same structure at three scales: the same boxes up to scale and rounding, the same answer to the same query
    const std::vector<double> scales = {0.001, 1.0, 1000.0};
    std::vector<std::vector<SurfaceBox>> scaled, queried;
    for (double s : scales)
    {
        CSGUnion *at = Scene();
        TRANSFORM t;
        Compute_Scaling_Transform(&t, Vector3d(s));
        Scale_Object(at, Vector3d(s), &t);
        Prepare(at);
        scaled.push_back(All(at));
        std::vector<SurfaceBox> found;
        Surface_Boxes(at, Vector3d(1.5, 0.2, 0.2) * s, Vector3d(4.5, 0.8, 0.8) * s, found);
        queried.push_back(found);
    }
    for (size_t k = 0; k < scales.size(); ++k)
    {
        assert((scaled[k].size() == scaled[1].size()) && (queried[k].size() == queried[1].size()));
        // single-precision boxes round outward by a few units in the last place of the scene's extent
        const double rounding = 1e-5 * 20.0 * scales[k];
        for (size_t j = 0; j < scaled[k].size(); ++j)
        {
            assert(scaled[k][j].piece == scaled[1][j].piece);
            for (int a = X; a <= Z; ++a)
            {
                assert(std::fabs(double(scaled[k][j].box.lowerLeft[a]) - double(scaled[1][j].box.lowerLeft[a]) * scales[k]) <= rounding);
                assert(std::fabs(double(scaled[k][j].box.size[a]) - double(scaled[1][j].box.size[a]) * scales[k]) <= rounding);
            }
        }
        for (size_t j = 0; j < queried[k].size(); ++j)
            assert(queried[k][j].piece == queried[1][j].piece);
    }
    std::printf("scale 0.001, 1, 1000: %zu boxes each, the query finds pieces", scaled[0].size());
    for (const SurfaceBox& b : queried[0])
        std::printf(" %u", b.piece);
    std::printf("\n");
    assert(queried[0].size() == 2);

    // ten thousand bricks: a query walks the child tree, and a hit finds its brick through it
    const int side = 100;
    std::vector<ObjectPtr> bricks;
    for (int i = 0; i < side; ++i)
        for (int j = 0; j < side; ++j)
            bricks.push_back(Brick(Vector3d(i * 2.0, 0, j * 2.0), Vector3d(i * 2.0 + 1, 1, j * 2.0 + 1)));
    CSGUnion *wall = Group(new CSGUnion(), bricks);
    Prepare(wall);
    assert(wall->Surface_Pieces() == std::uint32_t(side * side));
    class Counting final : public SurfaceBoxVisitor
    {
        public:
            unsigned entered = 0, visited = 0;
            std::vector<std::uint32_t> pieces;
            static bool Meets(const Vector3d& a, const Vector3d& b)
            {
                return (a.x() <= 51.0) && (b.x() >= 50.0) && (a.z() <= 51.0) && (b.z() >= 50.0);
            }
            bool Enter(const Vector3d& a, const Vector3d& b) override
            {
                ++entered;
                return Meets(a, b);
            }
            void Visit(const SurfaceBox& box) override
            {
                ++visited;
                Vector3d a, b;
                Make_min_max_from_BBox(a, b, box.box);
                if (Meets(a, b))
                    pieces.push_back(box.piece);
            }
    } counting;
    wall->Visit_Surface_Boxes(counting, 0);
    assert((counting.pieces.size() == 1) && (counting.pieces[0] == 25 * side + 25));
    std::printf("10000 bricks: a one-brick query enters %u tree nodes and visits %u bricks\n", counting.entered, counting.visited);
    assert(counting.visited < 100);

    TraceTicket ticket(8, 0.0);
    std::vector<Intersection> hits;
    std::mt19937_64 random(11);
    std::uniform_int_distribution<int> pick(0, side - 1);
    for (int r = 0; r < 1000; ++r)
    {
        const int i = pick(random), j = pick(random);
        Ray ray(ticket, Vector3d(i * 2.0 + 0.5, 5.0, j * 2.0 + 0.5), Vector3d(0, -1, 0));
        Intersection hit;
        const bool found = Find_Intersection(&hit, wall, ray, &thread);
        assert(found);
        hits.push_back(hit);
    }
    std::uint32_t sum = 0;
    const long long tree = Instructions([&]() {
        for (const Intersection& hit : hits)
        {
            std::uint32_t p = 0, l = 0;
            sum += Surface_Part(wall, hit, p, l) ? p : ~0u;
        }
    });
    const long long scan = Instructions([&]() {
        for (const Intersection& hit : hits)
        {
            std::uint32_t p = 0, l = 0;
            sum -= wall->Find_Surface_Leaf(hit.Object, Vector3d(std::numeric_limits<double>::quiet_NaN()), true, p, l) ? p : ~0u;
        }
    });
    assert(sum == 0);
    if (tree > 0)
        std::printf("10000 bricks: Surface_Part %lld instructions per hit through the tree, %lld scanning every member\n",
                    tree / (long long)hits.size(), scan / (long long)hits.size());
    else
        std::printf("10000 bricks: no instruction counter here\n");

    std::printf("surface boxes: ok\n");
    return 0;
}
