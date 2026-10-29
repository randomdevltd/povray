#include "core/lighting/lightsource.h"
#include "core/lighting/emitter.h"
#include "core/render/ray.h"
#include "core/render/trace.h"
#include "core/scene/object.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace pov;

class FixedEmitter final : public Emitter
{
public:
    explicit FixedEmitter(bool surface) : surface(surface) {}
    bool IsSurface() const override { return surface; }
    const MathColour& Intensity() const override { return intensity; }
    const Vector3d& Centre() const override { return centre; }
    double Radius() const override { return 0.5; }
    EmitterSample Sample(double, std::uint64_t, TraceThreadData*) const override { return EmitterSample(); }
private:
    bool surface;
    MathColour intensity = MathColour(1.0);
    Vector3d centre = Vector3d(0.0);
};

static void Near(double actual, double expected)
{
    if (std::abs(actual - expected) > 1e-12 * std::max(1.0, std::abs(expected)))
        std::cerr << "actual=" << actual << " expected=" << expected << '\n';
    assert(std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected)));
}

int main()
{
    for (bool surface : {true, false})
    {
        LightSource light;
        light.emitter = std::make_shared<FixedEmitter>(surface);
        light.lawRadius = light.emitter->Radius();
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket, Vector3d(0.0, 0.0, -4.0), Vector3d(0.0, 0.0, 1.0));
        for (double length : {0.1, 0.5, 2.0, 40.0, 3.0e6})
        {
            // A photon from an emitter already carries its physical flux; no path-length factor applies.
            Near(Light_Photon_Path_Weight(&light, length), 1.0);
            Near(Attenuate_Light(&light, ray, length), 1.0 / (std::max(length, 0.5) * std::max(length, 0.5)));
        }
        light.attenuationScale = 4.0;
        for (double length : {0.1, 2.0, 40.0})
            Near(Light_Photon_Path_Weight(&light, length), Light_Emitter_Path_Factor(&light, length));
        Near(Light_Photon_Path_Weight(&light, 1e9), 0.25);
        light.emitter.reset();
    }
    LightSource bare;
    bare.emitter = std::make_shared<FixedEmitter>(true);
    TraceTicket ticket(1, 0.0);
    Ray ray(ticket, Vector3d(0.0, 0.0, -4.0), Vector3d(0.0, 0.0, 1.0));
    Near(Attenuate_Light(&bare, ray, 2.0), 0.25);
    Near(Light_Photon_Path_Weight(&bare, 2.0), 1.0);
    bare.emitter.reset();
    LightSource point;
    Near(Light_Photon_Path_Weight(&point, 3.0), 9.0);
    std::cout << "Emitter light law: surface and volume photon path weights and attenuation pass\n";
}
