#ifndef POVRAY_CORE_PREPARATIONPROGRESS_H
#define POVRAY_CORE_PREPARATIONPROGRESS_H

#include "core/configcore.h"
#include "base/messenger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace pov
{

/// Reports scene preparation as `Preparation:` lines, whose format doc/PERF.md gives. One phase runs at a time;
/// Plan, Begin, Step, Poll and End run on the preparing thread, Advance on any.
class PreparationProgress final
{
public:
    /// `prefix` is how many columns the console puts before each line; a line and its prefix stay under 80.
    explicit PreparationProgress(pov_base::GenericMessenger *messages, std::size_t prefix, double interval = 2.0,
                                 double stall = 60.0) :
        messages(messages), width(kColumns - prefix), interval(interval), stall(stall) {}

    /// Each entry is a phase, or a phase and its estimated work units as `phase:units`.
    void Plan(const std::vector<std::string>& phases)
    {
        std::string line = "Preparation plan:", bare = line;
        for (const std::string& phase : phases)
        {
            line += " " + phase;
            bare += " " + phase.substr(0, phase.find(':'));
        }
        if ((messages != nullptr) && (phases.size() > 0))
            messages->Info("%s", (line.size() <= width ? line : bare).substr(0, width).c_str());
    }
    /// `counted`, if given, is work done that Advance does not count, read on the preparing thread when reporting.
    void Begin(const char *name, std::size_t phaseTotal, const std::function<std::size_t()>& counted = {})
    {
        phase = name;
        total = phaseTotal;
        extra = counted;
        step.clear();
        done.store(0, std::memory_order_relaxed);
        seen = 0;
        start = reported = moved = warned = Clock::now();
        Report(start);
    }
    /// Names where in the phase the work is, shown on the next line, shortened to fit.
    void Step(const std::string& text) { step = text; }
    void Advance(std::size_t count = 1) { done.fetch_add(count, std::memory_order_relaxed); }

    void Poll()
    {
        if (phase.empty())
            return;
        const Clock::time_point now = Clock::now();
        const std::size_t current = Done();
        if (current != seen)
        {
            seen = current;
            moved = warned = now;
        }
        if (Seconds(reported, now) >= interval)
            Report(now);
        if ((Seconds(moved, now) >= stall) && (Seconds(warned, now) >= stall))
        {
            warned = now;
            Emit(Format("stalled %.0fs at %lu/%lu %.1fs", Seconds(moved, now), Count(current), Count(total),
                        Seconds(start, now)), true);
        }
    }

    void End()
    {
        if (phase.empty())
            return;
        const Clock::time_point now = Clock::now();
        if (Done() != lastReported)
            Report(now);
        Emit(Format("finished %.1fs", Seconds(start, now)), false);
        phase.clear();
        extra = nullptr;
    }

private:
    using Clock = std::chrono::steady_clock;
    static const std::size_t kColumns = 78;
    static double Seconds(Clock::time_point from, Clock::time_point to) { return std::chrono::duration<double>(to - from).count(); }
    static unsigned long Count(std::size_t value) { return static_cast<unsigned long>(value); }
    static std::string Format(const char *format, ...)
    {
        char buffer[128];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        return buffer;
    }

    std::size_t Done() const { return done.load(std::memory_order_relaxed) + (extra ? extra() : 0); }
    void Report(Clock::time_point now)
    {
        reported = now;
        lastReported = Done();
        Emit(Format("%lu/%lu %.1fs", Count(lastReported), Count(total), Seconds(start, now)), true);
    }
    void Emit(const std::string& state, bool withStep) const
    {
        if (messages == nullptr)
            return;
        std::string line = "Preparation: " + phase + " " + state;
        if (withStep && !step.empty() && (line.size() + 2 < width))
            line += "; " + step.substr(0, width - line.size() - 2);
        messages->Info("%s", line.substr(0, width).c_str());
    }

    pov_base::GenericMessenger *messages;
    std::size_t width;
    double interval, stall;
    std::string phase, step;
    std::size_t total = 0, seen = 0, lastReported = 0;
    std::function<std::size_t()> extra;
    std::atomic<std::size_t> done{0};
    Clock::time_point start, reported, moved, warned;
};

/// Advances the preparation progress, if there is one.
inline void AdvancePreparation(PreparationProgress *progress, std::size_t count = 1)
{
    if (progress != nullptr)
        progress->Advance(count);
}

/// Names the step of the preparation progress, if there is one.
inline void StepPreparation(PreparationProgress *progress, const std::string& text)
{
    if (progress != nullptr)
        progress->Step(text);
}

}

#endif
