#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/sphere.h"
#include "core/shape/spheresweep.h"
#include "core/shape/cone.h"
#include "core/shape/csg.h"
#include "core/math/matrix.h"
#include "core/material/interior.h"
#include "core/material/noise.h"
#include "core/render/trace.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;

class InternalTrace final : public Trace
{
public:
    using Trace::Trace;
    using Trace::SubsurfaceInternalBoundaryAt;
};

static SphereSweep* Sweep(const Vector3d& first, double r0, const Vector3d& last, double r1)
{
    auto* result = new SphereSweep;
    result->Interpolation = LINEAR_SPHERE_SWEEP;
    result->Num_Modeling_Spheres = 2;
    result->Modeling_Sphere = static_cast<SPHSWEEP_SPH*>(POV_MALLOC(2 * sizeof(SPHSWEEP_SPH), "linear sweep spheres"));
    result->Modeling_Sphere[0] = {first, r0};
    result->Modeling_Sphere[1] = {last, r1};
    result->Compute();
    result->Compute_BBox();
    result->Type |= IS_CHILD_OBJECT;
    return result;
}

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    scene->preparedSets.emplace_back(new PreparedSet);
    CSGMerge hand;
    auto* palm = new Sphere;
    const Vector3d scale(0.025, 0.028, 0.009);
    TRANSFORM transform;
    Compute_Scaling_Transform(&transform, scale);
    palm->Scale(scale, &transform);
    Compute_Translation_Transform(&transform, Vector3d(0.0, -0.006, 0.0));
    palm->Transform(&transform);
    palm->Compute_BBox();
    hand.children.push_back(palm);
    auto* wrist = new Cone;
    wrist->apex = Vector3d(0.0, -0.026, 0.0); wrist->apex_radius = 0.015;
    wrist->base = Vector3d(0.0, -0.05, 0.0); wrist->base_radius = 0.011;
    wrist->Compute_Cone_Data();
    wrist->Compute_BBox();
    hand.children.push_back(wrist);
    for (int finger = 0; finger < 4; ++finger)
        hand.children.push_back(Sweep(Vector3d((finger - 1.5) * 0.012, 0.008, 0.0), 0.0055,
            Vector3d((finger - 1.5) * 0.015, 0.044 + 0.009 * std::sin(finger * M_PI / 3.0), 0.0), 0.0045));
    const Vector3d first(-0.019, -0.012, 0.0), last(-0.038, 0.009, 0.0);
    hand.children.push_back(Sweep(first, 0.007, last, 0.005));
    hand.interior = std::make_shared<Interior>();
    hand.interior->boundsLow = Vector3d(-0.05, -0.06, -0.012);
    hand.interior->boundsHigh = Vector3d(0.05, 0.06, 0.012);
    for (auto* child : hand.children)
    {
        child->Type |= IS_CHILD_OBJECT;
        child->interior = hand.interior;
    }
    hand.Compute_BBox();
    scene->preparedSets.front()->objects = {&hand};
    TraceThreadData thread(scene, 1, false);
    thread.subsurfaceCache = std::make_shared<SubsurfaceCache>();
    Trace::CooperateFunctor cooperate;
    Trace::MediaFunctor media;
    Trace::RadiosityFunctor radiosity;
    InternalTrace trace(scene, &thread, QualityFlags(9), cooperate, media, radiosity);
    TraceTicket ticket(1, 0.0);
    const Vector3d point(0.0024708888609845758, -0.042130803854291132, 0.0017057325958671587);
    const Vector3d direction(-0.70694367853398854, 0.70557646106237504, -0.048913116600022802);
    const auto boundary = trace.SubsurfaceInternalBoundaryAt(point, direction, wrist, &hand, ticket);
    auto implicit = [&](long double distance)
    {
        long double v[3], b[3], a = -(0.002L * 0.002L), h = 0.007L * -0.002L;
        for (int axis = 0; axis < 3; ++axis)
        {
            v[axis] = static_cast<long double>(point[axis]) + direction[axis] * distance - first[axis];
            b[axis] = static_cast<long double>(last[axis]) - first[axis];
            a += b[axis] * b[axis]; h += v[axis] * b[axis];
        }
        const long double u = std::max(0.0L, std::min(1.0L, h / a));
        const long double radius = 0.007L - 0.002L * u;
        long double value = -radius * radius;
        for (int axis = 0; axis < 3; ++axis) value += (v[axis] - u * b[axis]) * (v[axis] - u * b[axis]);
        return value;
    };
    long double low = 0.043L, high = 0.045L;
    assert(implicit(low) < 0.0L && implicit(high) > 0.0L);
    for (int i = 0; i < 80; ++i)
    {
        const long double middle = 0.5L * (low + high);
        (implicit(middle) < 0.0L ? low : high) = middle;
    }
    assert(std::abs(boundary.distance - static_cast<double>(0.5L * (low + high))) < 1e-12);
    assert(dot(direction, boundary.normal) > 0.0);
    assert(hand.Inside(boundary.position - boundary.normal * 1e-9, &thread));
    assert(!hand.Inside(boundary.position + boundary.normal * 1e-9, &thread));
    std::cout << "Internal transport through merged palm, wrist and linear fingers passed\n";
    Free_Noise_Tables();
}
