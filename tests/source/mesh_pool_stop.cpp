#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/mesh.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace pov;
int main()
{
    auto scene = std::make_shared<SceneData>();
    DeferredMeshPool pool(2);

    // builds that run until a stop interrupts them
    std::atomic<int> started(0);
    std::atomic<int> running(0);
    std::vector<std::shared_ptr<DeferredMeshState>> states;
    for (int i = 0; i < 6; ++i)
    {
        auto state = std::make_shared<DeferredMeshState>();
        state->work = [&started, &running](TraceThreadData *, DeferredMeshState& self) -> std::shared_ptr<Mesh>
        {
            started++;
            running++;
            while (!self.Cancelled())
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            running--;
            return nullptr;
        };
        states.push_back(state);
        pool.Submit(state, scene, i);
    }
    while (started < 2)
        std::this_thread::yield();

    // both stops wait for the in-flight builds: neither may return while a worker is still running
    std::thread a([&]() { pool.Stop(); });
    std::thread b([&]() { pool.Stop(); });
    a.join();
    b.join();
    assert(running == 0);
    assert(started == 2);

    // the workers' data is handed over once, and a second stop after it has nothing left to release
    std::vector<std::unique_ptr<TraceThreadData>> data = pool.Dispose();
    assert(data.size() == 2);
    assert(pool.Dispose().empty());
    std::cout << "mesh_pool_stop: ok" << std::endl;
    return 0;
}
