#include "core/lighting/subsurfaceboundary.h"
#include "core/render/trace.h"
#include "core/shape/box.h"
#include "core/shape/sphere.h"
#include "core/shape/csg.h"
#include "core/shape/mesh.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/material/noise.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;

class EntryTrace final : public Trace
{
public:
    using Trace::Trace;
    using Trace::ComputeSubsurfaceEntryIOR;
    using Trace::ComputeRelativeIOR;
    using Trace::ComputeFt;
};

// Two closed cubes in one mesh, so faces between them lie inside its bounds and its inside test casts a ray across them.
static void Cubes(Mesh& mesh, const Vector3d& offset)
{
    MeshBuilder builder(mesh, false);
    const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const Vector3d& at : {Vector3d(0.0), offset})
    {
        MeshIndex corner[8];
        for (int i = 0; i < 8; ++i)
            corner[i] = builder.Vertex(at + Vector3d((i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0), Vector3d(0, 0, 1));
        for (const auto& q : quads)
        {
            assert(builder.Triangle(corner[q[0]], corner[q[1]], corner[q[2]]));
            assert(builder.Triangle(corner[q[0]], corner[q[2]], corner[q[3]]));
        }
    }
    builder.Finish(true);
    mesh.Compute_BBox();
}

static void Near(double actual, double expected)
{
    if (std::abs(actual - expected) > 1e-12)
        std::cerr << "actual=" << actual << " expected=" << expected << '\n';
    assert(std::abs(actual - expected) < 1e-12);
}

static void Material(ObjectBase& object, double ior, unsigned precedence)
{
    object.interior = std::make_shared<Interior>();
    object.interior->IOR = ior;
    object.interior->iorState = kIorNumber;
    object.interior->iorMix = kIorMixReplace;
    object.interior->precedence = precedence;
    object.interior->boundsLow = Vector3d(-3.0);
    object.interior->boundsHigh = Vector3d(3.0);
}

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    scene->legacyIorStack = false;
    scene->dielectrics = true;
    scene->iorMixModes = true;
    scene->preparedSets.emplace_back(new PreparedSet);
    auto& objects = scene->preparedSets.front()->objects;
    TraceThreadData thread(scene, 1, false);
    Trace::CooperateFunctor cooperate;
    Trace::MediaFunctor media;
    Trace::RadiosityFunctor radiosity;
    EntryTrace trace(scene, &thread, QualityFlags(9), cooperate, media, radiosity);
    TraceTicket ticket(1, 0.0);
    Box water;
    water.bounds[0] = Vector3d(-10.0);
    water.bounds[1] = Vector3d(10.0, 10.0, 0.5);
    water.Compute_BBox();
    Material(water, 1.333, 1);
    Sphere body;
    body.Radius = 1.0;
    body.Compute_BBox();
    Material(body, 1.36, 2);
    objects = {&water, &body};
    const Vector3d down(0.0, 0.0, -1.0), up(0.0, 0.0, 1.0);
    const double waterIor = water.interior->IOR, bodyIor = body.interior->IOR;
    for (const Vector3d& source : {down, up, Vector3d(0.8, 0.0, -0.6)})
        Near(trace.ComputeSubsurfaceEntryIOR(down, down, &body, source, ticket), bodyIor / waterIor);
    Near(trace.ComputeSubsurfaceEntryIOR(up, up, &body, up, ticket), bodyIor);
    Near(trace.ComputeSubsurfaceEntryIOR(down, up, &body, down, ticket), bodyIor / waterIor);
    Ray eye(ticket, Vector3d(0.0, 0.0, 2.0), down);
    double exitEta;
    trace.ComputeRelativeIOR(eye, body.interior.get(), up, up, exitEta);
    Near(exitEta, bodyIor);
    assert(std::abs(exitEta - trace.ComputeSubsurfaceEntryIOR(down, down, &body, down, ticket)) > 0.1);

    body.interior->iorMix = kIorMixMean;
    Near(trace.ComputeSubsurfaceEntryIOR(down, down, &body, down, ticket), (bodyIor + waterIor) / (2.0 * waterIor));
    body.interior->iorMix = kIorMixSurface;
    Near(trace.ComputeSubsurfaceEntryIOR(down, down, &body, down, ticket), bodyIor / waterIor);
    body.interior->iorMix = kIorMixReplace;
    objects = {&body, &water};
    Near(trace.ComputeSubsurfaceEntryIOR(down, down, &body, down, ticket), bodyIor / waterIor);
    objects = {&water, &body};

    body.interior->IOR = water.interior->IOR;
    Near(trace.ComputeSubsurfaceEntryIOR(down, down, &body, down, ticket), 1.0);
    for (double cosine : {0.0, 1e-8, 0.1, 0.5, 1.0})
        Near(trace.ComputeFt(cosine, 1.0), 1.0);
    body.interior->IOR = bodyIor;
    objects = {&body};
    for (bool legacy : {false, true})
    {
        scene->legacyIorStack = legacy;
        for (const Vector3d& source : {up, down})
            Near(trace.ComputeSubsurfaceEntryIOR(up, up, &body, source, ticket), bodyIor);
        for (double cosine : {0.01, 0.1, 0.5, 1.0})
        {
            const double g = std::sqrt(bodyIor * bodyIor + cosine * cosine - 1.0);
            const double f = 0.5 * std::pow((g - cosine) / (g + cosine), 2.0) *
                (1.0 + std::pow((cosine * (g + cosine) - 1.0) / (cosine * (g - cosine) + 1.0), 2.0));
            Near(trace.ComputeFt(cosine, bodyIor), 1.0 - f);
        }
    }
    scene->legacyIorStack = false;
    for (double thickness : {0.2, 2e-8})
    {
        CSGIntersection shell(true);
        Sphere* outside = new Sphere;
        outside->Radius = 1.0 + thickness;
        outside->Compute_BBox();
        Sphere* cavity = new Sphere;
        cavity->Radius = 1.0;
        cavity->Compute_BBox();
        Set_Flag(cavity, INVERTED_FLAG);
        shell.children = {outside, cavity};
        shell.Compute_BBox();
        Material(shell, 1.36, 2);
        outside->interior = cavity->interior = shell.interior;
        objects = {&water, &shell};
        Near(trace.ComputeSubsurfaceEntryIOR(down, up, &shell, up, ticket), double(shell.interior->IOR) / waterIor);
    }
    const Vector3d offset(3.0, 3.0, 3.0);
    Mesh cubes;
    Cubes(cubes, offset);
    Material(cubes, 1.36, 2);
    cubes.interior->boundsLow = Vector3d(-3.0);
    cubes.interior->boundsHigh = Vector3d(6.0);
    objects = {&cubes};
    for (const Vector3d& at : {Vector3d(0.0), offset})
        for (int axis = 0; axis < 3; ++axis)
            for (double sign : {-1.0, 1.0})
            {
                Vector3d n(0.0), p = at + Vector3d(0.13, -0.27, 0.31);
                n[axis] = sign;
                p[axis] = at[axis] + sign;
                for (const Vector3d& facing : {n, -n})
                {
                    Near(trace.ComputeSubsurfaceEntryIOR(p, facing, &cubes, n, ticket), double(cubes.interior->IOR));
                    Near(trace.ComputeSubsurfaceEntryIOR(p, facing, &cubes, Vector3d(0.6, 0.0, 0.8) + n, ticket), double(cubes.interior->IOR));
                }
            }
    Near(trace.ComputeFt(0.1, 0.8), 0.0);
    assert(SubsurfaceFiniteBounds(Vector3d(-1.0), Vector3d(1.0)));
    assert(!SubsurfaceFiniteBounds(Vector3d(-BOUND_HUGE), Vector3d(BOUND_HUGE)));
    assert(!SubsurfaceFiniteBounds(Vector3d(0.0), Vector3d(0.0, 1.0, 1.0)));
    objects.clear();
    std::cout << "Local entry IOR: nested water, mixed air/water, equal IOR, shallow CSG cavity, closed mesh and air regression pass\n";
}
