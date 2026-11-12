#include "core/lighting/bounceprepare.h"
#include "core/material/noise.h"
#include "core/shape/sphere.h"
#include <cassert>
#include <iostream>

using namespace pov;
struct Messages : pov_base::GenericMessenger
{
    Messages() : GenericMessenger(5, "test") {}
    std::vector<std::string> lines;
    void SendMessage(pov_base::MessageClass, pov_base::WarningLevel, const char* text,
        const pov_base::UCS2String&, POV_LONG, POV_LONG, POV_OFF_T) override { lines.emplace_back(text); }
};
int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    assert(scene->surfaceDomainCells > 0.0);
    scene->preparedSets.emplace_back(new PreparedSet);
    TraceThreadData thread(scene, 0, false);
    Sphere object;
    object.source = std::make_shared<const std::string>("line 7 of surface_preparation.pov");
    Messages messages;
    PreparationProgress progress(&messages, 0, 0.0);
    thread.preparationProgress = &progress;
    scene->workThreads = 2;
    bounce_prepare::Workers workers(scene, &thread);
    progress.Begin("bounce", 1);
    workers.For(5, [] {}, [](std::size_t, std::size_t) {}, 0); // previously invisible resampling
    progress.Poll();
    assert(messages.lines.back().find("bounce 5/6") != std::string::npos);
    workers.For(3, [] {}, [](std::size_t, std::size_t) {}, 1); // every completed item advances
    progress.Poll();
    assert(messages.lines.back().find("bounce 8/8") != std::string::npos);
    progress.End();
    // A stalled line names the object a worker gathers on; finished gathers keep only the heaviest, never stopping one.
    PreparationProgress stalled(&messages, 0, 1000.0, 0.0);
    thread.preparationProgress = &stalled;
    scene->workThreads = 1;
    bounce_prepare::Workers solo(scene, &thread);
    stalled.Begin("bounce", 2);
    solo.For(2, [] {}, [&](std::size_t index, std::size_t worker)
    {
        solo[worker].Watch(&object);
        for (std::size_t step = 0; step < 3 + index; ++step)
            solo[worker].cooperate();
        stalled.Poll();
    }, 0);
    assert(messages.lines.back().find("bounce stalled") != std::string::npos &&
           messages.lines.back().find("gathering on sphere at line 7 of surface_preparation.pov") != std::string::npos);
    const bounce_prepare::Heaviest heaviest = solo.TakeHeaviest();
    assert(heaviest.object == &object && heaviest.steps == 4);
    // No count of steps stops a gather: #134's cap was 2^20.
    solo.For(1, [] {}, [&](std::size_t, std::size_t worker)
    {
        solo[worker].Watch(&object);
        for (std::size_t step = 0; step < (std::size_t(1) << 21); ++step)
            solo[worker].cooperate();
    }, 0);
    assert(solo.TakeHeaviest().steps == (std::size_t(1) << 21));
    assert(solo.TakeHeaviest().object == nullptr);
    stalled.End();
    thread.preparationProgress = nullptr;
    std::cout << "Surface preparation counts discovered work, names stalled gathers and reports the heaviest\n";
}
