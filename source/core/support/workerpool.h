#ifndef POVRAY_CORE_WORKERPOOL_H
#define POVRAY_CORE_WORKERPOOL_H

#include "core/configcore.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace pov
{

/// Threads made once and reused by every task, pass and parallel loop. A job starts at once: on an idle thread, or on a
/// new one when none is idle, so jobs that wait for each other cannot deadlock the pool.
class WorkerPool final
{
public:
    /// Never destroyed: its idle threads end with the process.
    static WorkerPool& Shared()
    {
        static WorkerPool* pool = new WorkerPool();
        return *pool;
    }

    /// `done`, if given, runs on the same thread once it counts as idle again, so a caller that waits for it can reuse it.
    void Run(std::function<void()> job, std::function<void()> done = {})
    {
        std::unique_lock<std::mutex> lock(mutex);
        jobs.emplace_back(std::move(job), std::move(done));
        if (idle < jobs.size())
        {
            threads.emplace_back(&WorkerPool::Work, this);
            idle++;
        }
        lock.unlock();
        ready.notify_one();
    }

    /// Threads made so far, for tests and statistics.
    std::size_t Threads()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return threads.size();
    }

private:
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::pair<std::function<void()>, std::function<void()>>> jobs;
    std::vector<std::thread> threads;
    std::size_t idle = 0;

    WorkerPool() = default;
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void Work()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (true)
        {
            ready.wait(lock, [this]() { return !jobs.empty(); });
            idle--;
            auto job(std::move(jobs.front()));
            jobs.pop_front();
            lock.unlock();
            job.first();
            job.first = nullptr;
            lock.lock();
            idle++;
            if (job.second)
            {
                lock.unlock();
                job.second();
                job.second = nullptr;
                lock.lock();
            }
        }
    }
};

}

#endif
