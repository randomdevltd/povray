// Linux user-space instruction microbenchmark; build like sphere_sweep_oracle.cpp.
// The projected spine is (u,u), so its distance from each test line is known exactly.
#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/spheresweep.h"
#include "core/material/noise.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

using namespace pov;

namespace
{
struct InstructionCounter
{
    int fd;
    InstructionCounter()
    {
        perf_event_attr attributes = {};
        attributes.size = sizeof(attributes);
        attributes.type = PERF_TYPE_HARDWARE;
        attributes.config = PERF_COUNT_HW_INSTRUCTIONS;
        attributes.exclude_kernel = 1;
        attributes.exclude_hv = 1;
        attributes.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd = syscall(SYS_perf_event_open, &attributes, 0, -1, -1, 0);
        if (fd < 0) { perror("perf_event_open"); std::exit(2); }
    }
    unsigned long long Read()
    {
        unsigned long long values[3];
        if (read(fd, values, sizeof(values)) != sizeof(values)) std::exit(2);
        if (values[1] != values[2]) { std::fprintf(stderr, "multiplexed\n"); std::exit(2); }
        return values[0];
    }
    ~InstructionCounter() { close(fd); }
};
}

int main(int argc, char** argv)
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    TraceTicket ticket(1, 0.0);
    InstructionCounter counter;
    const int repeats = argc > 1 ? std::atoi(argv[1]) : 5000;
    if (repeats <= 0) return 2;
    for (int kind = LINEAR_SPHERE_SWEEP; kind <= B_SPLINE_SPHERE_SWEEP; ++kind)
        for (double ratio : {0.01, 0.2})
        {
            SphereSweep sweep;
            sweep.Interpolation = kind;
            sweep.Num_Modeling_Spheres = kind == LINEAR_SPHERE_SWEEP ? 2 : 4;
            sweep.Modeling_Sphere = new SPHSWEEP_SPH[sweep.Num_Modeling_Spheres];
            const double radius = ratio * std::sqrt(2.0);
            for (int i = 0; i < sweep.Num_Modeling_Spheres; ++i)
            {
                const double u = kind == LINEAR_SPHERE_SWEEP ? i : i - 1;
                sweep.Modeling_Sphere[i].Center = Vector3d(u, u, 0.2 * u * u);
                sweep.Modeling_Sphere[i].Radius = radius;
            }
            sweep.Compute();
            for (int precise = 0; precise <= (kind == LINEAR_SPHERE_SWEEP); ++precise)
                for (int group = 0; group < 4; ++group)
                {
                    // Far miss (within the object box), 1.01-radius near miss, 0.5-radius hit.
                    Vector3d origin = group == 0 ? Vector3d(-0.9 * radius, 1 + 0.9 * radius, -3) :
                        Vector3d(0.3, 0.3 + std::sqrt(2.0) * radius * (group == 1 ? 1.01 : 0.5), -3);
                    // A forced line-only call isolates the work later discarded by the depth filter.
                    if (group == 3) origin[2] = 3;
                    const Vector3d direction(0, 0, 1);
                    SubsurfaceInternalRootQuery query(origin, direction, 0.0);
                    Ray ray(ticket, origin, direction, Ray::SubsurfaceRay);
                    // Reused ray addresses must not inherit a previous iteration's ticket slot.
                    ray.SetRootQuery(precise ? &query : nullptr);
                    IStack stack(thread.stackPool);
                    auto call = [&]()
                    {
                        sweep.All_Intersections(ray, stack, &thread);
                        while (!stack->empty()) stack->pop();
                    };
                    for (int i = 0; i < 100; ++i) call();
                    const auto start = counter.Read();
                    for (int i = 0; i < repeats; ++i) call();
                    const auto finish = counter.Read();
                    std::printf("kind=%d ratio=%.2g precise=%d class=%d instructions=%.2f\n",
                                kind, ratio, precise, group, double(finish - start) / repeats);
                }
        }
    return 0;
}
