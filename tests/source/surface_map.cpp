// Contract of the surface map: area unbiased against the truth with one box and many, each surface point counted once where
// pieces overlap or touch or a piece has several boxes, separated pieces cost their own boxes only, the same records on any
// thread count, from any reader and at any scale, "nothing found" and the precision stop returned, never thrown, every root
// found again (on moved copies too, with roots on cube faces), joints marked under any transform, and copies of one declared
// object sharing one map, made safely on several threads at once.
#include "core/lighting/surfacemapstats.h"
#include "core/material/noise.h"
#include "core/scene/scenedata.h"
#include "core/shape/box.h"
#include "core/shape/cone.h"
#include "core/shape/csg.h"
#include "core/shape/sphere.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <thread>
#include <unordered_set>

using namespace pov;
using surface_map::Map;

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

static Cone *Rod(const Vector3d& base, const Vector3d& apex, double radius)
{
    Cone *c = new Cone();
    c->Cylinder();
    c->base = base;
    c->apex = apex;
    c->apex_radius = c->base_radius = radius;
    c->Compute_Cylinder_Data();
    c->Compute_BBox();
    return c;
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
    std::unordered_set<ConstObjectPtr> visited;
    unsigned built = 0;
    Prepare_CSG_Child_Trees(csg, CSG_Child_Tree_Minimum(), visited, built);
    return csg;
}

// A union whose members are all one piece, reporting one surface box per member: a piece with several boxes.
struct OnePiece final : CSGUnion
{
    std::uint32_t Surface_Pieces() const override { return 1; }
    bool Find_Surface_Leaf(ConstObjectPtr leafObject, const Vector3d& point, bool, std::uint32_t& piece, std::uint32_t& leaf) const override
    {
        return CSGUnion::Find_Surface_Leaf(leafObject, point, false, piece, leaf);
    }
    void Visit_Surface_Boxes(SurfaceBoxVisitor& visitor, std::uint32_t firstPiece, const SurfaceQuery&) const override
    {
        for (ConstObjectPtr child : children)
        {
            SurfaceBox box;
            box.box = child->BBox;
            box.piece = firstPiece;
            visitor.Visit(box);
        }
    }
};

// Synthetic boxes exercise zero measure and unrelated piece IDs without changing the sphere's roots.
struct RegionSphere final : CSGUnion
{
    bool includeSurface = true;
    std::uint32_t unrelated = 0;
    void Visit_Surface_Boxes(SurfaceBoxVisitor& visitor, std::uint32_t firstPiece, const SurfaceQuery&) const override
    {
        SurfaceBox box;
        Make_BBox_from_min_max(box.box, Vector3d(0.0), Vector3d(0.0));
        box.piece = firstPiece;
        visitor.Visit(box);
        if (!includeSurface) return;
        box.box = BBox;
        visitor.Visit(box);
        for (std::uint32_t i = 1; i <= unrelated; ++i)
        {
            box.piece = firstPiece + i;
            visitor.Visit(box);
        }
        box.piece = firstPiece;
        visitor.Visit(box);
    }
};

// A brick under its own transform: turned by `own` and moved by `offset` first, then rotated, scaled and moved.
static Box *Moved(Box *b, const Vector3d& own, const Vector3d& offset, const Vector3d& rotate, double scale, const Vector3d& translate)
{
    TRANSFORM t;
    Compute_Rotation_Transform(&t, own);
    Rotate_Object(b, own, &t);
    Compute_Translation_Transform(&t, offset);
    Translate_Object(b, offset, &t);
    Compute_Rotation_Transform(&t, rotate);
    Rotate_Object(b, rotate, &t);
    Compute_Scaling_Transform(&t, Vector3d(scale));
    Scale_Object(b, Vector3d(scale), &t);
    Compute_Translation_Transform(&t, translate);
    Translate_Object(b, translate, &t);
    return b;
}

struct Totals final { double area = 0.0; std::size_t points = 0, lines = 0; std::uint64_t digest = 0; bool unique = true; };

// Every cube of level c read at `level`: the area its points stand for, its points, the lines traced and the ids' digest.
static Totals Read(Map& map, int c, int level, TraceThreadData& td)
{
    Totals t;
    std::set<std::uint64_t> ids;
    const std::int64_t n = std::int64_t(1) << c;
    for (std::int64_t i = 0; i < n; ++i)
        for (std::int64_t j = 0; j < n; ++j)
            for (std::int64_t k = 0; k < n; ++k)
            {
                const Map::Points p = map.Read(c, {i, j, k}, level, &td);
                assert(p.found != surface_map::Found::kPrecision);
                for (const Map::Part& part : p.parts)
                    for (const Map::Chunk* chunk : part.chunks)
                        for (std::size_t r = 0; r < chunk->records.size(); ++r)
                        {
                            t.area += part.weight;
                            ++t.points;
                            t.unique = ids.insert(Map::Id(part, *chunk, r)).second && t.unique;
                        }
            }
    std::size_t chunks, found;
    surface_map::Totals(map, chunks, t.lines, found, t.digest);
    return t;
}

// The area against the truth: within four standard deviations of a count of independent points (the sequence does better).
static void Check(const char *name, ObjectPtr object, double truth, int c, int level, TraceThreadData& td)
{
    Map map(object);
    std::vector<SurfaceBox> boxes;
    map.Boxes(boxes);
    const Totals t = Read(map, c, level, td);
    std::printf("%s,%zu,%d,%d,%zu,%zu,%.6g,%.6g\n", name, boxes.size(), c, level, t.lines, t.points, t.area, truth);
    assert(t.unique);
    assert(std::fabs(t.area - truth) <= 4.0 * truth / std::sqrt(double(t.points)));
}

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    scene->preparedSets.emplace_back(new PreparedSet);
    TraceThreadData td(scene, 0, false);
    std::printf("shape,boxes,cube level,level,lines,points,area,truth\n");

    // Zero-measure regions are empty; skipping one preserves the live region's lines and IDs.
    {
        RegionSphere synthetic;
        Group(&synthetic, {Ball(Vector3d(0.0), 1.0)});
        synthetic.includeSurface = false;
        Map empty(&synthetic);
        assert(empty.Read(0, {0, 0, 0}, 5, &td).found == surface_map::Found::kNothing);
        assert(empty.Read(0, {0, 0, 0}, std::numeric_limits<double>::digits + 2, &td).found == surface_map::Found::kPrecision);
        synthetic.includeSurface = true;
        Map ordinary(&synthetic);
        const Totals baseline = Read(ordinary, 0, 5, td);
        Sphere *sphere = Ball(Vector3d(0.0), 1.0);
        Map reference(sphere);
        const Totals expected = Read(reference, 0, 5, td);
        assert(baseline.points == expected.points && baseline.digest == expected.digest);
        synthetic.unrelated = 2048;
        Map many(&synthetic);
        const Map::Points points = many.Read(0, {0, 0, 0}, 0, &td);
        assert(points.parts.size() == 2050 && points.Count() > 0);
        assert(points.parts.front().region->Piece() == 0 && points.parts.back().region->Piece() == 0);
        for (const Map::Chunk* chunk : points.parts.back().chunks) assert(chunk->records.empty());
        for (std::size_t i = 1; i + 1 < points.parts.size(); ++i)
        {
            assert(points.parts[i].region->Piece() == i);
            for (const Map::Chunk* chunk : points.parts[i].chunks) assert(chunk->records.empty());
        }
        delete sphere;
    }

    // Area across levels, one box: sphere, box, capsule (a merge of three pieces), slab; many boxes: eight spheres apart.
    Sphere *sphere = Ball(Vector3d(0.0), 1.0);
    Box *box = Brick(Vector3d(0.0), Vector3d(2.0, 1.0, 0.5));
    CSGMerge *capsule = Group(new CSGMerge(), {Rod(Vector3d(0.0), Vector3d(2.0, 0.0, 0.0), 0.5), Ball(Vector3d(0.0), 0.5),
                                               Ball(Vector3d(2.0, 0.0, 0.0), 0.5)});
    Box *slab = Brick(Vector3d(0.0), Vector3d(4.0, 4.0, 0.05));
    std::vector<ObjectPtr> eight;
    for (int i = 0; i < 8; ++i) eight.push_back(Ball(Vector3d(i & 1, (i >> 1) & 1, i >> 2) * 2.0, 0.3));
    CSGUnion *apart = Group(new CSGUnion(), eight);
    for (int level = 3; level <= 7; ++level)
    {
        Check("sphere", sphere, 4.0 * M_PI, 0, level, td);
        Check("box", box, 2.0 * (2.0 + 1.0 + 0.5), 0, level, td);
        Check("capsule", capsule, 2.0 * M_PI * 0.5 * 2.0 + 4.0 * M_PI * 0.25, 0, level, td);
        Check("slab", slab, 2.0 * (16.0 + 0.2 + 0.2), 0, level, td);
        Check("eight spheres", apart, 8.0 * 4.0 * M_PI * 0.09, 0, level, td);
    }
    // The same in eight cubes: a lattice cube splits a box without double counting.
    Check("sphere", sphere, 4.0 * M_PI, 1, 7, td);

    // Overlapping and touching pieces: a union keeps both spheres' whole surfaces, a merge drops the cap of each inside the
    // other (2 pi R h with h = 1/2); flush bricks keep both faces of each joint, each once.
    CSGUnion *pair = Group(new CSGUnion(), {Ball(Vector3d(-0.5, 0.0, 0.0), 1.0), Ball(Vector3d(0.5, 0.0, 0.0), 1.0)});
    CSGMerge *merged = Group(new CSGMerge(), {Ball(Vector3d(-0.5, 0.0, 0.0), 1.0), Ball(Vector3d(0.5, 0.0, 0.0), 1.0)});
    CSGUnion *bricks = Group(new CSGUnion(), {Brick(Vector3d(0.0), Vector3d(1.0)), Brick(Vector3d(1.0, 0.0, 0.0), Vector3d(2.0, 1.0, 1.0))});
    std::vector<ObjectPtr> courses;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            courses.push_back(Brick(Vector3d(0.215 * i, 0.065 * j, 0.0), Vector3d(0.215 * (i + 1), 0.065 * (j + 1), 0.1025)));
    CSGUnion *wall = Group(new CSGUnion(), courses);
    for (int level = 5; level <= 7; ++level)
    {
        Check("union of two spheres", pair, 8.0 * M_PI, 0, level, td);
        Check("merge of two spheres", merged, 6.0 * M_PI, 0, level, td);
        Check("two flush bricks", bricks, 12.0, 0, level, td);
        Check("wall of 100 flush bricks", wall, 100.0 * 2.0 * (0.215 * 0.065 + 0.215 * 0.1025 + 0.065 * 0.1025), 0, level + 2, td);
    }

    // A piece with a box per member: two overlapping spheres, each point once (twice its area if each box's lines counted the
    // piece, and the overlap twice if each box kept what it holds).
    OnePiece *boxed = Group(new OnePiece(), {Ball(Vector3d(-0.5, 0.0, 0.0), 1.0), Ball(Vector3d(0.5, 0.0, 0.0), 1.0)});
    {
        std::vector<SurfaceBox> boxes;
        Map(boxed).Boxes(boxes);
        assert(boxes.size() == 2 && boxes[0].piece == boxes[1].piece);
    }
    for (int level = 5; level <= 7; ++level) Check("one piece in two boxes", boxed, 8.0 * M_PI, 0, level, td);
    Check("one piece in two boxes", boxed, 8.0 * M_PI, 1, 7, td);

    // Joints: flush bricks, each under its own transform, keep both faces of their joint marked though their depths there
    // round apart.
    for (int variant = 0; variant < 4; ++variant)
    {
        const Vector3d turn = variant > 0 ? Vector3d(30.0, 40.0, 50.0) : Vector3d(0.0);
        const double s = variant > 1 ? 0.37 : 1.0;
        const Vector3d shift = variant > 2 ? Vector3d(1000.0, -5.0, 7.0) : Vector3d(0.0);
        // the second brick is centred and turned a quarter in its own frame, so its face on the joint rounds its own way
        Box *left = Moved(Brick(Vector3d(0.0), Vector3d(1.0)), Vector3d(0.0), Vector3d(0.0), turn, s, shift);
        CSGUnion *flush = Group(new CSGUnion(), {left, Moved(Brick(Vector3d(-0.5), Vector3d(0.5)), Vector3d(90.0, 0.0, 0.0), Vector3d(1.5, 0.5, 0.5),
                                                             turn, s, shift)});
        Map map(flush);
        double joint = 0.0, area = 0.0;
        std::size_t joints = 0, onJoint = 0, unmarked = 0;
        // a point on the joint, in the left brick's own frame, to a record's single precision of the map's unit
        const double near = std::numeric_limits<float>::epsilon() * map.Unit() / s;
        for (const Map::Part& part : map.Read(0, {0, 0, 0}, 7, &td).parts)
            for (const Map::Chunk* chunk : part.chunks)
                for (const surface_map::Record& r : chunk->records)
                {
                    area += part.weight;
                    joint += r.joint ? part.weight : 0.0;
                    joints += r.joint;
                    Vector3d q;
                    MInvTransPoint(q, Map::Position(part, r), left->Trans);
                    if (std::fabs(q[X] - 1.0) <= near && q[Y] > near && q[Y] < 1.0 - near && q[Z] > near && q[Z] < 1.0 - near)
                    {
                        ++onJoint;
                        unmarked += !r.joint;
                    }
                }
        std::printf("flush bricks, transform %d: joint area %.4f of %.4f (truth %.4f of %.4f); %zu of %zu points on the joint unmarked\n",
                    variant, joint, area, 2.0 * s * s, 12.0 * s * s, unmarked, onJoint);
        assert(onJoint > 0 && unmarked == 0 && std::fabs(joint - 2.0 * s * s) <= 4.0 * 2.0 * s * s / std::sqrt(double(joints)));
        delete flush;
    }

    // Two tiny spheres far apart: lines go through their own boxes only.
    CSGUnion *far = Group(new CSGUnion(), {Ball(Vector3d(0.0), 0.01), Ball(Vector3d(100.0), 0.01)});
    for (int level = 12; level <= 14; ++level) Check("two tiny spheres far apart", far, 2.0 * 4.0 * M_PI * 1e-4, 0, level, td);

    // Any thread count, any reader: the same chunks, records and digest.
    {
        Map shared(pair), alone(pair), other(pair);
        const Totals one = Read(alone, 1, 7, td);
        const Totals again = Read(other, 1, 7, td);
        assert(one.digest == again.digest && one.points == again.points);
        std::vector<std::unique_ptr<TraceThreadData>> threads;
        std::vector<std::thread> running;
        std::vector<Map::Points> seen(8);
        for (std::size_t t = 0; t < seen.size(); ++t) threads.emplace_back(new TraceThreadData(scene, unsigned(t), false));
        for (std::size_t t = 0; t < seen.size(); ++t)
            running.emplace_back([&, t] { seen[t] = shared.Read(1, {std::int64_t(t & 1), std::int64_t((t >> 1) & 1), std::int64_t(t >> 2)}, 7, threads[t].get()); });
        for (std::thread& r : running) r.join();
        const Totals many = Read(shared, 1, 7, td);
        assert(many.digest == one.digest && many.points == one.points && many.lines == one.lines);
    }

    // Many threads joining the same chunks at once: claim, join and publish give one thread's records and bytes.
    {
        Map alone(pair), shared(pair);
        const Totals one = Read(alone, 0, 8, td);
        std::vector<std::unique_ptr<TraceThreadData>> threads;
        std::vector<std::thread> running;
        std::atomic<bool> go{false};
        for (unsigned t = 0; t < 8; ++t) threads.emplace_back(new TraceThreadData(scene, t, false));
        for (unsigned t = 0; t < 8; ++t)
            running.emplace_back([&, t] { while (!go.load()) std::this_thread::yield(); shared.Read(0, {0, 0, 0}, 8, threads[t].get()); });
        go = true;
        for (std::thread& r : running) r.join();
        const Totals many = Read(shared, 0, 8, td);
        std::printf("eight threads on one chunk: %zu points, digest %016llx; alone %016llx; %zu bytes\n", many.points,
                    (unsigned long long)many.digest, (unsigned long long)one.digest, shared.Bytes());
        assert(many.digest == one.digest && many.points == one.points && many.lines == one.lines);
        assert(shared.Bytes() == alone.Bytes() && alone.Bytes() > one.points * sizeof(surface_map::Record));
    }

    // Scale: the same ids, positions and areas up to the scale and rounding.
    {
        Totals base;
        for (double s : {0.001, 1.0, 1000.0})
        {
            Sphere *scaled = Ball(Vector3d(s * 0.25), s);
            Map map(scaled);
            const Totals t = Read(map, 0, 6, td);
            if (s == 0.001) base = t;
            std::printf("sphere at scale %g,1,0,6,%zu,%zu,%.9g,%.9g\n", s, t.lines, t.points, t.area / (s * s), 4.0 * M_PI);
            assert(t.digest == base.digest && t.points == base.points && t.lines == base.lines);
            assert(std::fabs(t.area / (s * s) - base.area / 1e-6) <= 1e-6 * base.area / 1e-6);
            delete scaled;
        }
    }

    // Each root found again: its leaf, at its record's place.
    {
        Map map(pair);
        const Map::Points p = map.Read(0, {0, 0, 0}, 5, &td);
        for (const Map::Part& part : p.parts)
        {
            std::vector<Intersection> hits;
            map.Hits(part, &td, hits);
            std::size_t n = 0;
            for (const Map::Chunk* chunk : part.chunks)
                for (const surface_map::Record& r : chunk->records)
                {
                    assert(hits[n].Object == map.Leaf(r.leaf));
                    assert((hits[n++].IPoint - Map::Position(part, r)).length() <= 1e-6 * map.Unit());
                }
        }
    }

    // Nothing found: a difference that removes everything; a lens read too coarse for its loose box, then finer. Past precision:
    // the precision stop. None of them throws.
    {
        CSGIntersection *none = new CSGIntersection(true);
        Sphere *cut = Ball(Vector3d(0.0), 2.0);
        Group(none, {Ball(Vector3d(0.0), 1.0), cut->Invert()});
        Map empty(none);
        for (int level = 0; level <= 8; ++level) assert(empty.Read(0, {0, 0, 0}, level, &td).found == surface_map::Found::kNothing);
        // the stock leaf's lens: two spheres of radius 16.68 at y = +-16.65, a lens of radius 1 in a box of 33 x 0.06 x 33
        CSGIntersection *lens = Group(new CSGIntersection(false), {Ball(Vector3d(0.0, 16.65, 0.0), 16.68), Ball(Vector3d(0.0, -16.65, 0.0), 16.68)});
        Map map(lens);
        int coarse = -1, fine = -1;
        for (int level = 0; level <= 16 && fine < 0; ++level)
        {
            if (map.Read(0, {0, 0, 0}, level, &td).found == surface_map::Found::kNothing) coarse = level;
            else fine = level;
        }
        std::printf("lens in its loose box: nothing found at level %d, points from level %d\n", coarse, fine);
        assert(coarse >= 0 && fine > coarse);
        assert(map.Read(0, {0, 0, 0}, std::numeric_limits<double>::digits + 2, &td).found == surface_map::Found::kPrecision);
        assert(map.Read(0, {2, 0, 0}, 4, &td).found == surface_map::Found::kNothing);
    }
    // Copies of one declared object share one map, traced through the first live copy: a moved copy's area through its
    // instance is its own, its roots are found again on itself, and clipping a copy takes it out of the lineage.
    {
        Box *declared = Brick(Vector3d(0.0), Vector3d(1.0));
        ObjectPtr first = Copy_Object(declared), second = Copy_Object(declared), clipped = Copy_Object(declared);
        TRANSFORM t;
        Compute_Translation_Transform(&t, Vector3d(5.0, 0.0, 0.0));
        Translate_Object(first, Vector3d(5.0, 0.0, 0.0), &t);
        Compute_Scaling_Transform(&t, Vector3d(1.0, 2.0, 3.0));
        Scale_Object(second, Vector3d(1.0, 2.0, 3.0), &t);
        Compute_Rotation_Transform(&t, Vector3d(30.0, 40.0, 50.0));
        Rotate_Object(second, Vector3d(30.0, 40.0, 50.0), &t);
        Leave_Lineage(clipped);
        delete declared;
        surface_map::Maps maps;
        const surface_map::Instance a = maps.Of(first), b = maps.Of(second), c = maps.Of(clipped);
        assert(&*a == &*b && &*c != &*a && !a.Moved() && b.Moved());
        double area = 0.0;
        const Map::Points p = b->Read(0, {0, 0, 0}, 6, &td);
        for (const Map::Part& part : p.parts)
        {
            std::vector<Intersection> hits;
            b.Hits(part, &td, hits);
            std::size_t n = 0;
            for (const Map::Chunk* chunk : part.chunks)
                for (const surface_map::Record& r : chunk->records)
                {
                    area += b.Weight(part, r);
                    assert(hits[n].Object == second && (hits[n++].IPoint - b.Position(part, r)).length() <= 1e-6 * b.Stretch());
                }
        }
        std::printf("moved copy of a unit cube, scaled <1,2,3>: area %.4f (truth 22); stretch %.17g, squeeze %.17g\n", area, b.Stretch(), b.Squeeze());
        // rotation keeps lengths: the longest and shortest a unit becomes are the scale's 3 and 1, to rounding (the scale's
        // condition 3 times the 3 terms of each product)
        const double rounding = 3.0 * 3.0 * std::numeric_limits<double>::epsilon();
        assert(std::fabs(b.Stretch() - 3.0) <= 3.0 * rounding && std::fabs(b.Squeeze() - 1.0) <= rounding && a.Stretch() == 1.0 && a.Squeeze() == 1.0);
        assert(std::fabs(area - 22.0) <= 4.0 * 22.0 / std::sqrt(double(p.Count())));
        std::size_t chunks, traced, found, count;
        std::uint64_t digest;
        surface_map::Totals(maps, chunks, traced, found, digest, nullptr, &count);
        assert(count == 2);
    }
    // A moved copy's roots found again where they lie on the faces of the cubes of levels 1 and 2.
    {
        Map frame(Brick(Vector3d(0.0), Vector3d(1.0)));
        const double half = frame.Origin()[X] + frame.Spacing(1), quarter = frame.Origin()[Y] + frame.Spacing(2);
        CSGUnion *declared = Group(new CSGUnion(), {Brick(Vector3d(0.0), Vector3d(1.0)), Brick(Vector3d(half, quarter, 0.25), Vector3d(1.0))});
        ObjectPtr first = Copy_Object(declared), second = Copy_Object(declared);
        TRANSFORM t;
        Compute_Rotation_Transform(&t, Vector3d(30.0, 40.0, 50.0));
        Rotate_Object(second, Vector3d(30.0, 40.0, 50.0), &t);
        Compute_Scaling_Transform(&t, Vector3d(1.0, 2.0, 3.0));
        Scale_Object(second, Vector3d(1.0, 2.0, 3.0), &t);
        Compute_Translation_Transform(&t, Vector3d(1000.0, -5.0, 7.0));
        Translate_Object(second, Vector3d(1000.0, -5.0, 7.0), &t);
        surface_map::Maps maps;
        const surface_map::Instance a = maps.Of(first), b = maps.Of(second);
        assert(&*a == &*b && b.Moved());
        std::size_t found = 0, lost = 0;
        double worst = 0.0;
        for (int c = 1; c <= 2; ++c)
            for (std::int64_t i = 0; i < (std::int64_t(1) << c); ++i)
                for (std::int64_t j = 0; j < (std::int64_t(1) << c); ++j)
                    for (std::int64_t k = 0; k < (std::int64_t(1) << c); ++k)
                        for (const Map::Part& part : b->Read(c, {i, j, k}, c + 5, &td).parts)
                        {
                            std::vector<Intersection> hits;
                            b.Hits(part, &td, hits);
                            std::size_t n = 0;
                            for (const Map::Chunk* chunk : part.chunks)
                                for (const surface_map::Record& r : chunk->records)
                                {
                                    const Intersection& hit = hits[n++];
                                    ++found;
                                    if (hit.Object == nullptr) { ++lost; continue; }
                                    worst = std::max(worst, (hit.IPoint - b.Position(part, r)).length());
                                }
                        }
        // a record keeps its place to single precision of the map's unit, which the copy stretches
        std::printf("moved copy with faces on cube faces: %zu roots found again, %zu lost, worst %.3g\n", found, lost, worst);
        assert(found > 0 && lost == 0 && worst <= std::numeric_limits<float>::epsilon() * b->Unit() * b.Stretch());
    }

    // Copies made on several threads at once of an object with no lineage yet: one lineage holding every copy.
    {
        Box *original = Brick(Vector3d(0.0), Vector3d(1.0));
        std::vector<ObjectPtr> copies(8);
        std::vector<std::thread> running;
        std::atomic<bool> go{false};
        for (std::size_t t = 0; t < copies.size(); ++t)
            running.emplace_back([&, t] { while (!go.load()) std::this_thread::yield(); copies[t] = Copy_Object(original); });
        go = true;
        for (std::thread& r : running) r.join();
        const std::shared_ptr<SurfaceLineage> lineage = original->surfaceCopy.lineage;
        assert(lineage != nullptr && lineage->live.size() == copies.size() + 1);
        for (ObjectPtr copy : copies) { assert(copy->surfaceCopy.lineage == lineage); delete copy; }
        delete original;
    }
    std::printf("surface map: pass\n");
}
