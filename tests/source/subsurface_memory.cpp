#include "core/lighting/subsurface.h"
#include "core/scene/scenedata.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

using namespace pov;

namespace
{

// What /proc/meminfo states for field, in bytes; 0 when it states nothing.
double Memory(const char *field)
{
    std::ifstream meminfo("/proc/meminfo");
    std::string name;
    double kilobytes;
    while (meminfo >> name >> kilobytes)
    {
        if (name == field)
            return kilobytes * 1024.0;
        meminfo.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0.0;
}

}

int main()
{
    // the clamp itself, at and between its bounds
    assert(SubsurfaceDefaultMemoryMB(0.0) == kSubsurfaceMemoryFloorMB);
    assert(SubsurfaceDefaultMemoryMB(kSubsurfaceMemoryFloorMB / kSubsurfaceMemoryShare * 1048576.0) == kSubsurfaceMemoryFloorMB);
    assert(SubsurfaceDefaultMemoryMB(8.0 * (1 << 30)) == 2048.0);  // a quarter of 8 GB
    assert(SubsurfaceDefaultMemoryMB(16.0 * (1 << 30)) == 4096.0); // a quarter of 16 GB
    assert(SubsurfaceDefaultMemoryMB(128.0 * (1 << 30)) == kSubsurfaceMemoryCeilingMB);
    double at = 0.0;
    for (double gigabytes = 0.0; gigabytes <= 256.0; gigabytes += 1.0 / 1024.0)
    {
        const double cap = SubsurfaceDefaultMemoryMB(gigabytes * (1 << 30));
        assert(cap >= at && cap <= kSubsurfaceMemoryCeilingMB); // never shrinks as memory grows
        at = cap;
    }

    // the default the scene starts from is machine-sized: exactly a quarter of the machine's total memory, clamped,
    // on any machine (total memory is fixed while the machine runs, so both reads of it agree)
    const double cap = SubsurfaceDefaultMemoryMB();
    const double quarterTotal = kSubsurfaceMemoryShare * Memory("MemTotal:") / 1048576.0;
    assert(cap == std::max(kSubsurfaceMemoryFloorMB, std::min(kSubsurfaceMemoryCeilingMB, quarterTotal)));
    assert(cap >= kSubsurfaceMemoryFloorMB && cap <= kSubsurfaceMemoryCeilingMB);
    assert(SceneData().subsurfaceMemory == cap);
    std::cout << "subsurface memory default " << cap << " MB passed\n";
    return 0;
}
