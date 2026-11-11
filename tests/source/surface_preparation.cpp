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
    scene->preparedSets.emplace_back(new PreparedSet);
    TraceThreadData thread(scene, 0, false);
    Sphere object;
    object.source = std::make_shared<const std::string>("line 7 of surface_preparation.pov");
    bounce_prepare::Context context(scene, &thread, [] {});
    context.Watch(&object);
    context.Check();
    context.started -= std::chrono::seconds(31);
    bool rejected = false;
    try { context.cooperate(); }
    catch (const std::runtime_error& e)
    {
        const std::string message(e.what());
        rejected = message.find("30 seconds at one gather") != std::string::npos &&
                   message.find(*object.source) != std::string::npos;
    }
    assert(rejected);
    context.owner = nullptr;
    context.cooperate();
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
    thread.preparationProgress = nullptr;
    std::cout << "Surface preparation counts resampling and identifies stalled gathers\n";
}
