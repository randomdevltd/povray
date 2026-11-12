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
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    assert(scene->surfaceDomainCells == 4096.0);
    scene->preparedSets.emplace_back(new PreparedSet);
    TraceThreadData thread(scene, 0, false);
    Sphere object;
    object.source = std::make_shared<const std::string>("line 7 of surface_preparation.pov");
    bounce_prepare::Context context(scene, &thread, [] {});
    context.Watch(&object);
    for (std::size_t step = 0; step < bounce_prepare::Context::kMaximumGatherSteps; ++step)
        context.cooperate(); // the entire work allowance succeeds, regardless of elapsed time
    assert(context.work == bounce_prepare::Context::kMaximumGatherSteps);
    bool rejected = false;
    try { context.cooperate(); }
    catch (const std::runtime_error& e)
    {
        const std::string message(e.what());
        rejected = message.find("trace steps at one gather") != std::string::npos &&
                   message.find(*object.source) != std::string::npos;
    }
    assert(rejected);
    context.Watch(&object); // a new gather gets its full allowance
    context.cooperate();
    assert(context.work == 1);
    context.owner = nullptr;
    context.work = bounce_prepare::Context::kMaximumGatherSteps;
    context.cooperate(); // work outside a gather is not charged
    Messages messages;
    PreparationProgress progress(&messages, 0, 0.0);
    thread.preparationProgress = &progress;
    scene->workThreads = 2;
    bounce_prepare::Workers workers(scene, &thread);
    progress.Begin("bounce", 1);
    workers.For(5, [] {}, [](std::size_t, std::size_t) {}, 0); // discovered work without prepaid units
    progress.Poll();
    assert(messages.lines.back().find("bounce 5/6") != std::string::npos);
    workers.For(3, [] {}, [](std::size_t, std::size_t) {}, 1); // every completed item advances
    progress.Poll();
    assert(messages.lines.back().find("bounce 8/8") != std::string::npos);
    // Completing at the work limit must not charge an extra step or discard the result.
    workers.For(1, [] {}, [&](std::size_t, std::size_t worker)
    {
        workers[worker].Watch(&object);
        workers[worker].work = bounce_prepare::Context::kMaximumGatherSteps;
    });
    for (const auto& worker : workers.contexts) assert(worker->owner == nullptr);
    progress.End();
    thread.preparationProgress = nullptr;
    std::cout << "Surface preparation counts discovered work, bounds gathers and identifies runaway objects\n";
}
