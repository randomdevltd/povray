#include "core/support/parallel.h"
#include "core/support/workerpool.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace pov;
int main()
{
    // the caller is worker 0 and every index runs once
    std::vector<std::atomic<int>> hits(1000);
    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<int> callerItems(0);
    ParallelFor(hits.size(), 4, [&](size_t i, size_t worker)
    {
        hits[i]++;
        if (worker == 0)
            callerItems += (std::this_thread::get_id() == caller) ? 1 : -1000000;
    });
    for (auto& h : hits)
        assert(h == 1);
    assert(callerItems > 0);

    // later loops reuse the pool's threads instead of making new ones
    const size_t made = WorkerPool::Shared().Threads();
    for (int round = 0; round < 50; ++round)
        ParallelFor(64, 4, [](size_t, size_t) { std::this_thread::yield(); });
    assert(WorkerPool::Shared().Threads() <= made + 3);

    // a loop inside an item waits for its own workers without deadlocking the pool
    std::atomic<int> inner(0);
    ParallelFor(8, 8, [&](size_t, size_t)
    {
        ParallelFor(16, 4, [&](size_t, size_t) { inner++; });
    });
    assert(inner == 8 * 16);

    // jobs that wait for each other all start
    std::atomic<int> arrived(0);
    std::atomic<int> left(0);
    for (int i = 0; i < 6; ++i)
        WorkerPool::Shared().Run([&]() { arrived++; while (arrived < 6) std::this_thread::yield(); left++; });
    while (left < 6)
        std::this_thread::yield();

    // the lowest failing index is reported, and every index below it ran
    std::vector<std::atomic<int>> ran(200);
    try
    {
        ParallelFor(ran.size(), 4, [&](size_t i, size_t)
        {
            ran[i]++;
            if ((i == 37) || (i == 150))
                throw std::runtime_error(std::to_string(i));
        });
        assert(false);
    }
    catch (const std::runtime_error& e)
    {
        assert(std::string(e.what()) == "37");
    }
    for (size_t i = 0; i < 37; ++i)
        assert(ran[i] == 1);

    // a stop raised by cooperation on the caller ends the loop early and is rethrown once workers are done
    std::atomic<int> started(0);
    bool stoppedEarly = false;
    try
    {
        ParallelFor(100000, 4, [&](size_t, size_t) { started++; std::this_thread::sleep_for(std::chrono::microseconds(50)); },
                    [&]() { if (started > 100) throw 7; }, true);
    }
    catch (int code)
    {
        stoppedEarly = (code == 7);
    }
    assert(stoppedEarly && (started < 100000));
    std::cout << "worker_pool: ok" << std::endl;
    return 0;
}
