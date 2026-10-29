#ifndef POVRAY_CORE_PARALLEL_H
#define POVRAY_CORE_PARALLEL_H

#include "core/configcore.h"

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

// Calls function(index, worker) for every index below count. With `watched`, cooperate is also called between items
// when one thread does them all. The failure reported is that of the lowest failing index, else cooperate's.
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
    std::atomic<std::size_t> running(threadCount);
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
        }
        if (running.fetch_sub(1, std::memory_order_release) == 1)
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            done.notify_one();
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    try
    {
        for (std::size_t i = 0; i < threadCount; ++i)
            workers.emplace_back(run, i);
    }
    catch (...)
    {
        recordStop();
        running.fetch_sub(threadCount - workers.size(), std::memory_order_release);
        for (std::thread& worker : workers)
            worker.join();
        throw;
    }
    while (running.load(std::memory_order_acquire) != 0)
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        done.wait_for(lock, std::chrono::milliseconds(100), [&]() { return running.load(std::memory_order_acquire) == 0; });
        lock.unlock();
        if (!stopped.load(std::memory_order_relaxed) && (running.load(std::memory_order_acquire) != 0))
            try { cooperate(); } catch (...) { recordStop(); }
    }
    for (std::thread& worker : workers)
        worker.join();
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
