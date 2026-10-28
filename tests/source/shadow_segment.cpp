#include "core/math/intersectiondepth.h"
#include "core/shape/sphere.h"
#include "core/shape/box.h"
#include "core/shape/mesh.h"
#include "core/shape/isosurface.h"
#include "core/shape/quadric.h"
#include "core/shape/csg.h"
#include "core/render/shadowsegment.h"
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
    explicit SphereFunction(int kind = 0) : kind(kind) {}
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
        if (kind == 1) return -1.0;
        if (kind == 2) return a[2];
        return a[0]*a[0] + a[1]*a[1] + a[2]*a[2] - 0.25;
    }
    GenericScalarFunction* Clone() const override { return new SphereFunction(kind); }
private:
    int kind;
};


struct Result
{
    std::vector<double> roots;
    double cursor = 0.0;
    double remaining;
};

Result walk(const std::vector<ObjectPtr>& objects, const Vector3d& from, const Vector3d& target, TraceThreadData& thread)
{
    const double end = (target - from).length();
    ShadowSegmentWalker walker(end, 0.001);
    TraceTicket ticket(8, 0.0);
    Ray query(ticket, from, (target - from) / end, Ray::OtherRay, true);
    query.SetRootQuery(&walker);
    Result result;
    result.remaining = walker.Remaining();
    for (size_t testBudget = 0; testBudget < 2000; ++testBudget)
    {
        walker.Reset();
        Intersection best;
        best.Depth = end;
        bool found = false;
        for (ObjectPtr object : objects)
        {
            Intersection hit;
            if (Find_Intersection_BBox_Passed(&hit, object, query, walker, &thread) && hit.Depth < best.Depth)
            {
                best = hit;
                found = true;
            }
        }
        if (!found)
        {
            result.cursor = walker.Cursor();
            return result;
        }
        const double previous = walker.Cursor();
        assert(best.Depth >= previous && best.Depth < end);
        Ray segment(query);
        assert(!segment.GetRootQuery() && !segment.GetRootOwner());
        segment.Origin = from + query.Direction * previous;
        const double segmentDepth = best.Depth - previous;
        assert(segmentDepth >= 0.0 && IsFiniteIntersectionDepth(segmentDepth));
        result.roots.push_back(best.Depth);
        if (Test_Flag(best.Object, OPAQUE_FLAG))
        {
            result.cursor = best.Depth;
            return result;
        }
        assert(walker.Commit(best.Depth));
        assert(!walker.Commit(best.Depth));
        const double remaining = walker.Remaining();
        assert(remaining <= result.remaining);
        result.remaining = remaining;
        assert(result.remaining >= 0.0);
    }
    assert(false && "production helper exceeded the bounded test budget");
    return result;
}

void meshLayers(Mesh& mesh, const std::vector<double>& depths)
{
    MeshBuilder builder(mesh, false);
    for (double z : depths)
    {
        const Vector3d n(0.0, 0.0, -1.0);
        const auto a = builder.Vertex(Vector3d(-1.0, -1.0, z), n);
        const auto b = builder.Vertex(Vector3d(1.0, -1.0, z), n);
        const auto c = builder.Vertex(Vector3d(0.0, 1.0, z), n);
        assert(builder.Triangle(a,b,c));
    }
    builder.Finish(false);
    mesh.Compute_BBox();
    mesh.Build_Mesh_BBox_Tree();
}

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    const Vector3d origin(0.0), end(0.0, 0.0, 10.0);
    Sphere sun[2];
    const Vector3d centre = Vector3d(0.3835,0.5751,-0.7225).normalized() * 1.496e11;
    for (Sphere& s : sun) { s.Center = centre; s.Radius = 6.957e8; s.Compute_BBox(); }
    const Vector3d p(-38857654.22050982,229052769.13303345,-321390513.48943233);
    const Vector3d q(57830566835.73434,85711787512.22798,-108335489459.76096);
    for (auto objects : {std::vector<ObjectPtr>{&sun[0],&sun[1]}, std::vector<ObjectPtr>{&sun[1],&sun[0]}})
    {
        const auto r = walk(objects,p,q,thread);
        assert(r.roots.size() == 2 && r.roots[0] == r.roots[1]);
        std::cout.precision(17);
        std::cout << "solar events=" << r.roots.size() << " cursor=" << r.cursor << " remaining=" << r.remaining << "\n";
    }
    Box touching[2];
    touching[0].bounds[0] = Vector3d(-1,-1,1); touching[0].bounds[1] = Vector3d(1,1,2);
    touching[1].bounds[0] = Vector3d(-1,-1,2); touching[1].bounds[1] = Vector3d(1,1,3);
    for (auto objects : {std::vector<ObjectPtr>{&touching[0],&touching[1]},std::vector<ObjectPtr>{&touching[1],&touching[0]}})
    {
        const auto r = walk(objects,origin,end,thread);
        assert((r.roots == std::vector<double>{1,2,2,3}));
    }
    touching[1].bounds[0][2] += 1e-5;
    auto close = walk({&touching[0],&touching[1]},origin,end,thread);
    assert(close.roots.size() == 4 && close.roots[2] > close.roots[1]);
    assert(close.roots[2]-close.roots[1] < MIN_ISECT_DEPTH);
    std::vector<std::unique_ptr<Box>> storage;
    std::vector<ObjectPtr> many;
    for (int i=0;i<300;++i)
    {
        storage.emplace_back(new Box);
        auto& b = *storage.back();
        b.bounds[0] = Vector3d(-1,-1,1+i*.02); b.bounds[1] = Vector3d(1,1,1.01+i*.02);
        many.push_back(&b);
    }
    assert(walk(many,origin,end,thread).roots.size() == 600);
    Set_Flag(many[100],OPAQUE_FLAG);
    assert(walk(many,origin,end,thread).roots.size() == 201);
    Mesh mesh;
    meshLayers(mesh,{1,2,3});
    auto layered = walk({&mesh},origin,end,thread);
    assert(layered.roots.size() == 3);
    assert(walk({&mesh},origin,Vector3d(0,0,1),thread).roots.empty());
    assert(walk({&mesh},origin,Vector3d(0,0,std::nextafter(1.0,2.0)),thread).roots.size() == 1);
    Mesh bound;
    meshLayers(bound,{0.5});
    mesh.Bound.push_back(&bound);
    assert(walk({&mesh},origin,end,thread).roots.size() == 3);
    mesh.Bound.clear();
    CSGUnion group;
    group.children.push_back(&mesh);
    assert(walk({&group},origin,end,thread).roots.size() == 3);
    group.children.clear();
    Mesh scaled;
    meshLayers(scaled,{1,2,3});
    TRANSFORM transform;
    Compute_Scaling_Transform(&transform, Vector3d(2,3,2));
    scaled.Scale(Vector3d(2,3,2),&transform);
    Compute_Translation_Transform(&transform, Vector3d(0,0,10));
    scaled.Translate(Vector3d(0,0,10),&transform);
    auto scaledRoots = walk({&scaled},origin,Vector3d(0,0,20),thread);
    assert((scaledRoots.roots == std::vector<double>{12,14,16}));
    IsoSurface iso;
    iso.Function = new SphereFunction;
    iso.max_gradient = 4.0; iso.accuracy = 1e-7; iso.max_trace = 1;
    auto roots = walk({&iso},Vector3d(0,0,-3),Vector3d(0,0,3),thread);
    assert(roots.roots.size() == 2);
    assert(std::abs(roots.roots[0]-2.5) < 1e-6 && std::abs(roots.roots[1]-3.5) < 1e-6);
    IsoSurface twin;
    twin.Function = new SphereFunction;
    twin.max_gradient = 4.0; twin.accuracy = 1e-7; twin.max_trace = 1;
    for (auto objects : {std::vector<ObjectPtr>{&iso,&twin},std::vector<ObjectPtr>{&twin,&iso}})
    {
        auto pair = walk(objects,Vector3d(0,0,-3),Vector3d(0,0,3),thread);
        assert(pair.roots.size() == 4 && pair.roots[0] == pair.roots[1] && pair.roots[2] == pair.roots[3]);
    }
    group.children.push_back(&iso);
    assert(walk({&group},Vector3d(0,0,-3),Vector3d(0,0,3),thread).roots.size() == 2);
    group.children.clear();
    for (int kind : {1,2})
    {
        IsoSurface capped;
        capped.Function = new SphereFunction(kind);
        capped.max_gradient = 4.0; capped.accuracy = 1e-7; capped.max_trace = 1;
        auto caps = walk({&capped},Vector3d(0,0,-3),Vector3d(0,0,3),thread);
        assert(caps.roots.size() == 2 && std::abs(caps.roots[0]-2.0) < 1e-6);
        assert(std::abs(caps.roots[1]-(kind == 1 ? 4.0 : 3.0)) < 1e-6);
    }
    Box clip;
    clip.bounds[0] = Vector3d(-1,-1,-.5); clip.bounds[1] = Vector3d(1,1,.5);
    IsoSurface clipped;
    clipped.Function = new SphereFunction(2);
    clipped.max_gradient = 4.0; clipped.accuracy = 1e-7; clipped.max_trace = 1;
    clipped.Clip.push_back(&clip);
    auto clipRoots = walk({&clipped},Vector3d(0,0,-3),Vector3d(0,0,3),thread);
    assert(clipRoots.roots.size() == 1 && std::abs(clipRoots.roots[0]-3.0) < 1e-6);
    clipped.Clip.clear();
    clip.bounds[0][2] = 0.0; clip.bounds[1][2] = .75;
    IsoSurface clippedSphere;
    clippedSphere.Function = new SphereFunction;
    clippedSphere.max_gradient = 4.0; clippedSphere.accuracy = 1e-7; clippedSphere.max_trace = 1;
    clippedSphere.Clip.push_back(&clip);
    auto clippedExit = walk({&clippedSphere},Vector3d(0,0,-3),Vector3d(0,0,3),thread);
    assert(clippedExit.roots.size() == 1 && std::abs(clippedExit.roots[0]-3.5) < 1e-6);
    clippedSphere.Clip.clear();
    Compute_Scaling_Transform(&transform, Vector3d(2,3,4));
    iso.Scale(Vector3d(2,3,4),&transform);
    Compute_Translation_Transform(&transform, Vector3d(0,0,10));
    iso.Translate(Vector3d(0,0,10),&transform);
    auto scaledIso = walk({&iso},origin,Vector3d(0,0,20),thread);
    assert(scaledIso.roots.size() == 2 && std::abs(scaledIso.roots[0]-8) < 5e-6 && std::abs(scaledIso.roots[1]-12) < 5e-6);
    ShadowSegmentWalker scoped(10.0,0.001);
    TraceTicket ticket(8,0.0);
    Ray borrowed(ticket,origin,Vector3d(0,0,1));
    borrowed.SetRootQuery(&scoped);
    {
        RayRootOwnerScope outer(borrowed,&mesh);
        RayRootOwnerScope inner(borrowed,&iso);
        assert(borrowed.GetRootOwner() == &mesh);
        {
            RayRootQueryDisabled occupancy(borrowed);
            assert(!borrowed.GetRootQuery() && !borrowed.GetRootOwner());
        }
        assert(borrowed.GetRootQuery() == &scoped && borrowed.GetRootOwner() == &mesh);
        Ray escaped(borrowed);
        assert(!escaped.GetRootQuery() && !escaped.GetRootOwner());
    }
    assert(!borrowed.GetRootOwner());
    std::cout << "touching, close layers, 600 transparent events, opaque stop, endpoints, mesh, CSG and iso max_trace=1 passed\n";
}
