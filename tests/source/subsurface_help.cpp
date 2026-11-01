#include "core/lighting/subsurface.h"
#include "core/material/noise.h"
#include "core/render/trace.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace pov;
int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData threadData(scene, 1, false);
    Trace::CooperateFunctor cooperate;
    Trace::MediaFunctor media;
    Trace::RadiosityFunctor radiosity;
    QualityFlags quality(9);
    Trace trace(scene, &threadData, quality, cooperate, media, radiosity);
    SubsurfaceCache cache;

    // a builder offers a cell's jobs; three helpers take them until the builder withdraws the cell
    std::atomic<int> jobs(64);
    std::mutex seenMutex;
    std::vector<int> seen;
    SubsurfaceCell built;
    std::atomic<bool> withdrawn(false);
    std::vector<std::thread> helpers;
    std::atomic<int> idleRounds(0);
    for (int h = 0; h < 3; ++h)
    {
        helpers.emplace_back([&]()
        {
            while (!withdrawn)
            {
                if (!cache.Help(trace, std::chrono::milliseconds(20)))
                    idleRounds++;
            }
        });
    }
    cache.Offer(&built, [&](Trace&)
    {
        int job = jobs.fetch_sub(1, std::memory_order_relaxed);
        if (job <= 0)
            return false;
        std::lock_guard<std::mutex> lock(seenMutex);
        seen.push_back(job);
        return true;
    });
    while (jobs > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache.Withdraw(&built);
    withdrawn = true;
    for (std::thread& h : helpers)
        h.join();
    std::sort(seen.begin(), seen.end());
    assert(seen.size() == 64);
    for (int i = 0; i < 64; ++i)
        assert(seen[i] == i + 1);
    assert(idleRounds > 0);

    // a helper's job that throws fails the helper, not the builder, and the offers survive it
    SubsurfaceCell failing;
    std::atomic<int> thrown(0);
    std::thread builder([&]()
    {
        cache.Offer(&failing, [&](Trace&) -> bool
        {
            throw std::runtime_error("cell job failed");
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        cache.Withdraw(&failing);
        thrown++;
    });
    bool caught = false;
    try
    {
        cache.Help(trace, std::chrono::milliseconds(2000));
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    builder.join();
    assert(caught);
    assert(thrown == 1);
    assert(cache.Help(trace, std::chrono::milliseconds(0)) == false);
    std::cout << "subsurface_help: ok" << std::endl;
    return 0;
}
