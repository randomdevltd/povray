#ifndef POVRAY_CORE_PARALLEL_H
#define POVRAY_CORE_PARALLEL_H

#include "core/configcore.h"
#include "core/support/workerpool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace pov
{

// Calls function(index, worker) for every index below count, the caller being worker 0. With `watched`, cooperate is
// called between the caller's items, else at most every 100 ms. The failure reported is the lowest failing index's, else cooperate's.
template<typename Function, typename Cooperate>
void ParallelFor(std::size_t count, std::size_t threadCount, const Function& function, const Cooperate& cooperate,
                 bool watched = false)
{
    threadCount = std::max<std::size_t>(1, std::min(count, threadCount));
    if (count == 0)
        return;
    if (threadCount == 1)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            function(i, 0);
            if (watched && (i + 1 < count))
                cooperate();
        }
        return;
    }

    std::atomic<std::size_t> next(0);
    std::atomic<std::size_t> limit(count);
    std::atomic<bool> stopped(false);
    std::atomic<std::size_t> running(threadCount - 1);
    std::exception_ptr failure, stop;
    std::size_t failureIndex = count;
    std::mutex failureMutex;
    std::mutex doneMutex;
    std::condition_variable done;
    auto recordFailure = [&](std::size_t index)
    {
        std::lock_guard<std::mutex> lock(failureMutex);
        if (index < failureIndex)
        {
            failureIndex = index;
            failure = std::current_exception();
            limit.store(index, std::memory_order_relaxed);
        }
    };
    auto recordStop = [&]()
    {
        std::lock_guard<std::mutex> lock(failureMutex);
        if (!stop)
            stop = std::current_exception();
        stopped.store(true, std::memory_order_relaxed);
    };
    auto lastCooperation = std::chrono::steady_clock::now();
    auto cooperateEvery = [&](std::chrono::milliseconds interval)
    {
        const auto now = std::chrono::steady_clock::now();
        if (stopped.load(std::memory_order_relaxed) || (now - lastCooperation < interval))
            return;
        lastCooperation = now;
        try { cooperate(); } catch (...) { recordStop(); }
    };
    auto run = [&](std::size_t worker)
    {
        while (!stopped.load(std::memory_order_relaxed))
        {
            const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= limit.load(std::memory_order_relaxed))
                break;
            try
            {
                function(i, worker);
            }
            catch (...)
            {
                recordFailure(i);
            }
            if (worker == 0)
                cooperateEvery(std::chrono::milliseconds(watched ? 0 : 100));
        }
    };
    auto finished = [&]()
    {
        // notified under the lock, so the caller cannot return while a worker still holds it
        std::lock_guard<std::mutex> lock(doneMutex);
        if (running.fetch_sub(1, std::memory_order_acq_rel) == 1)
            done.notify_one();
    };
    auto waitForWorkers = [&](bool cooperating)
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        while (running.load(std::memory_order_acquire) != 0)
        {
            done.wait_for(lock, std::chrono::milliseconds(100), [&]() { return running.load(std::memory_order_acquire) == 0; });
            if (cooperating && (running.load(std::memory_order_acquire) != 0))
            {
                lock.unlock();
                cooperateEvery(std::chrono::milliseconds(100));
                lock.lock();
            }
        }
    };

    // the caller is worker 0; the others come from the shared pool
    std::size_t launched = 1;
    try
    {
        for (; launched < threadCount; ++launched)
            WorkerPool::Shared().Run([&, launched]() { run(launched); }, finished);
    }
    catch (...)
    {
        recordStop();
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            running.fetch_sub(threadCount - launched, std::memory_order_acq_rel);
        }
        waitForWorkers(false);
        throw;
    }
    run(0);
    waitForWorkers(true);
    if (failure)
        std::rethrow_exception(failure);
    if (stop)
        std::rethrow_exception(stop);
}

template<typename Function>
void ParallelFor(std::size_t count, std::size_t threadCount, const Function& function)
{
    ParallelFor(count, threadCount, function, []() {});
}

}

#endif
