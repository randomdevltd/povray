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

template<typename Function, typename Cooperate>
void ParallelFor(std::size_t count, std::size_t threadCount, const Function& function, const Cooperate& cooperate)
{
    threadCount = std::max<std::size_t>(1, std::min(count, threadCount));
    if (threadCount == 1)
    {
        for (std::size_t i = 0; i < count; ++i)
            function(i, 0);
        return;
    }

    std::atomic<std::size_t> next(0);
    std::atomic<bool> failed(false);
    std::atomic<std::size_t> running(threadCount);
    std::exception_ptr failure;
    std::mutex failureMutex;
    std::mutex doneMutex;
    std::condition_variable done;
    auto recordFailure = [&]()
    {
        std::lock_guard<std::mutex> lock(failureMutex);
        if (!failure)
            failure = std::current_exception();
        failed.store(true, std::memory_order_relaxed);
    };
    auto run = [&](std::size_t worker)
    {
        try
        {
            while (!failed.load(std::memory_order_relaxed))
            {
                const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= count)
                    break;
                function(i, worker);
            }
        }
        catch (...)
        {
            recordFailure();
        }
        if (running.fetch_sub(1, std::memory_order_release) == 1)
            done.notify_one();
    };

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
        workers.emplace_back(run, i);
    while (running.load(std::memory_order_acquire) != 0)
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        done.wait_for(lock, std::chrono::milliseconds(100), [&]() { return running.load(std::memory_order_acquire) == 0; });
        lock.unlock();
        if (!failed.load(std::memory_order_relaxed) && (running.load(std::memory_order_acquire) != 0))
            try { cooperate(); } catch (...) { recordFailure(); }
    }
    for (std::thread& worker : workers)
        worker.join();
    if (failure)
        std::rethrow_exception(failure);
}

template<typename Function>
void ParallelFor(std::size_t count, std::size_t threadCount, const Function& function)
{
    ParallelFor(count, threadCount, function, []() {});
}

}

#endif
