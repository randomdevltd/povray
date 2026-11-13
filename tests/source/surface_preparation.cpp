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
    const std::vector<double> masses{0.0, 1.0, 9.0};
    const auto first = surface_refine::ChooseSource(masses, 0.1);
    const auto middle = surface_refine::ChooseSource(masses, 0.3);
    const auto last = surface_refine::ChooseSource(masses, std::nextafter(1.0, 0.0));
    assert(first.index == 0 && std::abs(first.probability - 1.0 / 6.0) < 1e-15);
    assert(middle.index == 1 && std::abs(middle.probability - (1.0 / 6.0 + 0.05)) < 1e-15);
    assert(last.index == 2 && std::abs(last.probability - (1.0 / 6.0 + 0.45)) < 1e-15);
    const auto dark = surface_refine::ChooseSource({0.0, 0.0}, 0.75);
    const auto overflow = surface_refine::ChooseSource({HUGE_VAL, 1.0}, 0.75);
    assert(dark.index == 1 && dark.probability == 0.5);
    assert(overflow.index == 1 && overflow.probability == 0.5);
    LightSource sun, faded;
    sun.Parallel = true;
    sun.Center = faded.Center = Vector3d(0.0, 1.0e6, 0.0);
    faded.Fade_Power = 2.0;
    faded.Fade_Distance = 0.0;
    const double near = surface_refine::SourceMass(sun, Vector3d(0.0, 1.0e6 - 1.0, 0.0));
    assert(near > 0.0 && surface_refine::SourceMass(sun, Vector3d(0.0)) == near);
    assert(std::abs(surface_refine::SourceMass(faded, Vector3d(0.0, 1.0e6 - 1.0, 0.0)) /
                    surface_refine::SourceMass(faded, Vector3d(0.0, 1.0e6 - 2.0, 0.0)) - 4.0) < 1e-9);
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
