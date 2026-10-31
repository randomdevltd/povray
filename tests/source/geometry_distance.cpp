#include "core/math/intersectiondepth.h"
#include "core/shape/sphere.h"
#include "core/shape/box.h"
#include "core/shape/mesh.h"
#include "core/shape/isosurface.h"
#include "core/shape/quadric.h"
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/render/trace.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <typeinfo>

using namespace pov;

struct SphereContext final : GenericFunctionContext
{
    double args[3];
    int next = 0;
};

class SphereFunction final : public GenericScalarFunction
{
public:
    GenericFunctionContextPtr AcquireContext(TraceThreadData*) override { return new SphereContext; }
    void ReleaseContext(GenericFunctionContextPtr c) override { delete c; }
    void InitArguments(GenericFunctionContextPtr c) override { static_cast<SphereContext*>(c)->next = 0; }
    void PushArgument(GenericFunctionContextPtr c, double a) override
    {
        auto& s = *static_cast<SphereContext*>(c);
        s.args[s.next++] = a;
    }
    double Execute(GenericFunctionContextPtr c) override
    {
        const auto& a = static_cast<SphereContext*>(c)->args;
        return a[0]*a[0] + a[1]*a[1] + a[2]*a[2] - 0.25;
    }
    GenericScalarFunction* Clone() const override { return new SphereFunction; }
};

struct Segment final : RayObjectCondition
{
    double end;
    explicit Segment(double e) : end(e) {}
    bool operator()(const Ray&, ConstObjectPtr, double depth) const override { return depth < end; }
};

void checkSceneTrees(ObjectBase& object, Ray& ray, TraceThreadData& thread, double entry, double tolerance)
{
    BBOX_TREE leaf{};
    leaf.Node = reinterpret_cast<BBOX_TREE**>(&object);
    leaf.BBox = object.BBox;
    std::unique_ptr<FlatBBoxTree> flat(Build_Flat_BBox_Tree(&leaf));
    BBoxPriorityQueue queue;
    for (bool shadow : {false, true})
    {
        Ray query(ray);
        query.SetFlags(shadow ? Ray::OtherRay : Ray::PrimaryRay, shadow);
        for (double end : {entry - tolerance * 4, entry + tolerance * 4, HUGE_VAL})
        {
            Intersection legacy, blocked;
            legacy.Depth = blocked.Depth = end;
            const bool expected = end > entry;
            const bool oldTree = Intersect_BBox_Tree(queue, &leaf, query, &legacy, &thread);
            const bool flatTree = Intersect_Flat_BBox_Tree(*flat, query, &blocked, &thread);
            if (oldTree != expected || flatTree != expected)
            {
                std::cerr << typeid(object).name() << " scene trees at " << entry << " end " << end
                          << " expected " << expected << " legacy " << oldTree << " flat " << flatTree << "\n";
            }
            assert(oldTree == expected);
            assert(flatTree == expected);
            if (expected)
            {
                assert(std::abs(legacy.Depth - entry) <= tolerance);
                assert(std::abs(blocked.Depth - entry) <= tolerance);
            }
        }
    }
}

void check(ObjectBase& object, Ray& ray, TraceThreadData& thread, double entry, double tolerance)
{
    IStack hits(thread.stackPool);
    const bool found = object.All_Intersections(ray, hits, &thread);
    if (!found)
        std::cerr << typeid(object).name() << " missed entry " << entry << "\n";
    assert(found);
    double nearest = HUGE_VAL;
    while (!hits->empty())
    {
        assert(IsFiniteIntersectionDepth(hits->top().Depth));
        nearest = std::min(nearest, hits->top().Depth);
        hits->pop();
    }
    assert(std::abs(nearest - entry) <= tolerance);
    Intersection hit;
    const bool closestFound = Find_Intersection(&hit, &object, ray, &thread);
    if (!closestFound)
        std::cerr << typeid(object).name() << " nearest query missed entry " << entry << "\n";
    assert(closestFound);
    assert(std::abs(hit.Depth - entry) <= tolerance);
    const Segment before(entry - tolerance * 4), after(entry + tolerance * 4);
    assert(!Find_Intersection(&hit, &object, ray, before, &thread));
    assert(Find_Intersection(&hit, &object, ray, after, &thread));
    Ray shadow(ray);
    shadow.SetFlags(Ray::OtherRay, true);
    assert(!Find_Intersection(&hit, &object, shadow, before, &thread));
    assert(Find_Intersection(&hit, &object, shadow, after, &thread));
}

void move(ObjectBase& object, const Vector3d& scale, const Vector3d& translation)
{
    TRANSFORM transform;
    Compute_Scaling_Transform(&transform, scale);
    object.Scale(scale, &transform);
    Compute_Translation_Transform(&transform, translation);
    object.Translate(translation, &transform);
}

template<typename Block>
void checkSlabs(Block block, TraceThreadData& thread)
{
    block.count = 1;
    block.more = 0;
    block.child[0] = -1;
    FlatBBoxTreeOf<Block> tree;
    tree.blocks.push_back(block);
    for (double dx : {1e-32, -1e-32, 0.0})
    {
        const BasicRay ray(Vector3d(0.0), Vector3d(dx, 0.0, 1.0));
        FlatBBoxWalker<BasicRay> walker(ray, 0.0f);
        walker.Expand(tree, 0, 200.0f, thread.Stats());
        assert((walker.size == 1) == (dx > 0.0));
    }
    const BasicRay inside(Vector3d(1.5e-30, 0.0, 0.0), Vector3d(0.0, 0.0, 1.0));
    FlatBBoxWalker<BasicRay> walker(inside, 0.0f);
    walker.Expand(tree, 0, 200.0f, thread.Stats());
    assert(walker.size == 1);
}

int main()
{
    struct NoiseTables
    {
        NoiseTables() { Initialize_Noise(); }
        ~NoiseTables() { Free_Noise_Tables(); }
    } noise;
    assert(!IsFiniteIntersectionDepth(std::numeric_limits<double>::infinity()));
    assert(!IsFiniteIntersectionDepth(-std::numeric_limits<double>::infinity()));
    assert(!IsFiniteIntersectionDepth(std::numeric_limits<double>::quiet_NaN()));
    assert(IsFiniteIntersectionDepth(std::numeric_limits<double>::max()));
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    FlatBBoxBlock full{};
    full.lo[0][0] = 1e-30f; full.hi[0][0] = 2e-30f;
    full.lo[1][0] = -1.0f; full.hi[1][0] = 1.0f;
    full.lo[2][0] = 100.0f; full.hi[2][0] = 101.0f;
    checkSlabs(full, thread);
    FlatQBBoxBlock quantized{};
    for (int d = 0; d < 3; ++d)
    {
        quantized.origin[d] = full.lo[d][0];
        quantized.scale[d] = (full.hi[d][0] - full.lo[d][0]) / 65535.0f;
        quantized.hi[d][0] = 65535;
    }
    checkSlabs(quantized, thread);
    TraceTicket ticket(8, 0.0);
    Ray ray(ticket, Vector3d(0.0), Vector3d(0.0, 0.0, 1.0));
    for (double distance : {100.0, 3.844e8, 1.496e11})
    {
        const double radius = distance * 0.004;
        const double entry = distance - radius;
        const double tolerance = distance * 2e-6;
        for (double sign : {-1.0, 1.0})
        {
            BoundingBox forward, inverse;
            Make_BBox_from_min_max(forward, Vector3d(-radius), Vector3d(radius));
            inverse = forward;
            TRANSFORM translation;
            Compute_Translation_Transform(&translation, Vector3d(0.0, 0.0, sign * distance));
            Recompute_BBox(&forward, &translation);
            Recompute_Inverse_BBox(&inverse, &translation);
            assert(std::abs(forward.lowerLeft[2] - (sign * distance - radius)) <= tolerance);
            assert(std::abs(inverse.lowerLeft[2] - (-sign * distance - radius)) <= tolerance);
            assert(std::abs(forward.size[2] - 2 * radius) <= tolerance);
            assert(std::abs(inverse.size[2] - 2 * radius) <= tolerance);
        }
        Sphere sphere;
        sphere.Center = Vector3d(0.0, 0.0, distance);
        sphere.Radius = radius;
        sphere.Compute_BBox();
        check(sphere, ray, thread, entry, tolerance);
        checkSceneTrees(sphere, ray, thread, entry, tolerance);
        Sphere small;
        small.Center = Vector3d(0.0, 0.0, distance);
        small.Compute_BBox();
        check(small, ray, thread, distance - 1.0, 1e-4);
        Quadric linear;
        linear.Square_Terms = Vector3d(0.0);
        linear.Terms = Vector3d(0.0, 0.0, 1.0);
        linear.Constant = -entry;
        Vector3d quadricMin(-BOUND_HUGE), quadricMax(BOUND_HUGE);
        linear.Compute_BBox(quadricMin, quadricMax);
        check(linear, ray, thread, entry, tolerance);
        IStack linearHits(thread.stackPool);
        assert(linear.All_Intersections(ray, linearHits, &thread));
        assert(linearHits->size() == 1);
        linearHits->pop();
        Box excluded;
        excluded.bounds[0] = Vector3d(1.0, 1.0, entry - 1.0);
        excluded.bounds[1] = Vector3d(2.0, 2.0, entry + 1.0);
        excluded.Compute_BBox();
        Intersection clipped;
        linear.Bound.push_back(&excluded);
        assert(!Find_Intersection(&clipped, &linear, ray, &thread));
        linear.Bound.clear();
        linear.Clip.push_back(&excluded);
        assert(!Find_Intersection(&clipped, &linear, ray, &thread));
        linear.Clip.clear();
        Sphere transformed;
        move(transformed, Vector3d(radius * 2, radius, radius), Vector3d(0.0, 0.0, distance));
        check(transformed, ray, thread, entry, tolerance);
        checkSceneTrees(transformed, ray, thread, entry, tolerance);
        Box box;
        box.bounds[0] = Vector3d(-radius, -radius, entry);
        box.bounds[1] = Vector3d(radius, radius, distance + radius);
        box.Compute_BBox();
        check(box, ray, thread, entry, tolerance);
        checkSceneTrees(box, ray, thread, entry, tolerance);
        for (bool local : {false, true})
        {
            Mesh mesh;
            MeshBuilder builder(mesh, false);
            const double r = local ? 1.0 : radius;
            const double z = local ? 0.0 : entry;
            const Vector3d n(0.0, 0.0, -1.0);
            auto a = builder.Vertex(Vector3d(-r, -r, z), n);
            auto b = builder.Vertex(Vector3d(r, -r, z), n);
            auto c = builder.Vertex(Vector3d(0.0, r, z), n);
            assert(builder.Triangle(a, b, c));
            builder.Finish(false);
            mesh.Compute_BBox();
            mesh.Build_Mesh_BBox_Tree();
            if (local)
                move(mesh, Vector3d(radius), Vector3d(0.0, 0.0, entry));
            check(mesh, ray, thread, entry, tolerance);
            checkSceneTrees(mesh, ray, thread, entry, tolerance);
        }
        IsoSurface iso;
        iso.Function = new SphereFunction;
        iso.max_gradient = 4.0;
        iso.accuracy = 1e-7;
        move(iso, Vector3d(radius * 2), Vector3d(0.0, 0.0, distance));
        check(iso, ray, thread, entry, tolerance);
        checkSceneTrees(iso, ray, thread, entry, tolerance);
        std::cout << "geometry distance " << distance << ": sphere, ellipsoid, box, meshes and isosurface passed\n";
    }
    Quadric bounded;
    bounded.Constant = -1.0;
    bounded.Compute_BBox();
    assert(bounded.Automatic_Bounds);
    check(bounded, ray, thread, 1.0, 1e-6);
}
