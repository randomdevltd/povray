#include "core/lighting/subsurface.h"
#include "core/scene/scenedata.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

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

    // the views of one scene share its clouds: a cell one view built serves the next, which counts it as reused
    int object = 0;
    auto key = [&](std::int64_t x) { return SubsurfaceCellKey{ &object, 0, x, 0, 0, false, false, 0, 0 }; };
    auto store = std::make_shared<SubsurfaceStore>(1.0);
    SubsurfaceCache first(store);
    bool build = false;
    std::shared_ptr<SubsurfaceCell> cell = first.Acquire(key(0), build);
    assert(build);
    cell->points.reserve(600000 / sizeof(SubsurfacePoint));
    cell->stage = SubsurfaceCell::kReady;
    cell->usable = true;
    first.Charge(*cell);
    std::shared_ptr<SubsurfaceCell> stopped_cell = first.Acquire(key(1), build);
    assert(build);
    SubsurfaceCache second(store);
    assert(second.View() == first.View() + 1);
    assert(second.Acquire(key(0), build) == cell && !build);
    // a cell a stopped view left unfinished is built again by the next
    assert(second.Acquire(key(1), build) != stopped_cell && build);

    // what depends on the view keeps cells apart: cameras that space points alike share an id, others do not
    first.SetCamera(Vector3d(0, 1, -4), 0.0, 0.01);
    second.SetCamera(Vector3d(0, 1, -4), 0.0, 0.01);
    SubsurfaceCache third(store);
    third.SetCamera(Vector3d(3, 2, 2), 0.0, 0.01);
    Vector3d at;
    double size, angle;
    std::uint64_t a, b, c;
    first.GetCamera(at, size, angle, a);
    second.GetCamera(at, size, angle, b);
    third.GetCamera(at, size, angle, c);
    assert((a != 0) && (a == b) && (c != a));
    first.PhotonsShot(true);
    second.PhotonsShot(false);
    third.PhotonsShot(true);
    assert((first.PhotonShot() == second.PhotonShot()) && (third.PhotonShot() != first.PhotonShot()));

    // the cap holds the views' clouds together: a cell that fits alone but not beside an earlier view's stops the render
    std::shared_ptr<SubsurfaceCell> more = third.Acquire(key(2), build);
    more->points.reserve(600000 / sizeof(SubsurfacePoint));
    assert(third.CellFits(more->Bytes()));
    stopped = false;
    try { third.Charge(*more); }
    catch (const std::runtime_error& error)
    {
        stopped = true;
        assert(std::strstr(error.what(), "past their memory cap of 1 MB") != nullptr);
    }
    assert(stopped);
    std::cout << "subsurface memory cap, set by the scene alone and shared by its views, passed\n";
    return 0;
}
