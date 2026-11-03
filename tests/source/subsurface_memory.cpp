#include "core/lighting/subsurface.h"
#include "core/scene/scenedata.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace pov;

int main()
{
    // a scene that sets no cap starts uncapped: the clouds take what they need, and only the system's own memory
    // failure can stop the render
    const SceneData scene;
    assert(scene.subsurfaceMemory == kSubsurfaceUncappedMB);

    // uncapped, a single cell far past any cap the scene could have set still fits, and nothing stops the render
    SubsurfaceCache uncapped;
    assert(uncapped.CellFits(size_t(8) << 30));
    uncapped.CheckCap();

    // a cap the scene sets stops the render once a cell alone passes it, and the error names the declared cap
    SubsurfaceCache capped(1.0);
    assert(capped.CellFits(1024));
    assert(!capped.CellFits(size_t(2) << 20));
    bool stopped = false;
    try { capped.CheckCap(); }
    catch (const std::runtime_error& error)
    {
        stopped = true;
        assert(std::strstr(error.what(), "past their memory cap of 1 MB") != nullptr);
    }
    assert(stopped);
    std::cout << "subsurface memory cap, set by the scene alone, passed\n";
    return 0;
}
