#include "core/lighting/progressivephotonmap.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
int receivers[3];
pov::ConstObjectPtr Receiver(unsigned int index)
{
    return reinterpret_cast<pov::ConstObjectPtr>(&receivers[index]);
}

void Grid(pov::ProgressivePhotonMap& map, unsigned int receiver, double spacing, double height, double power)
{
    for (int x = -24; x <= 24; ++x)
        for (int z = -24; z <= 24; ++z)
        {
            pov::ProgressivePhoton photon;
            photon.point = pov::Vector3d(x * spacing, height, z * spacing);
            photon.normal = photon.direction = pov::Vector3d(0, 1, 0);
            photon.flux = pov::MathColour(power * (1.0 + 10.0 * std::exp(-(x * x + z * z) / 32.0)));
            photon.object = Receiver(receiver);
            map.photons.push_back(photon);
        }
}
}

int main()
{
    pov::ProgressivePhotonMap separate, combined, bright;
    Grid(separate, 0, 0.01, 0, 1);
    Grid(combined, 0, 0.01, 0, 1);
    Grid(combined, 1, 0.2, 0.001, 1);
    Grid(bright, 0, 0.01, 0, 2);
    separate.Build(0, 2);
    combined.Build(0, 2);
    bright.Build(0, 2);
    const pov::Vector3d point(0, 0, 0);
    size_t visited = 0;
    assert(combined.RadiusAt(point, Receiver(2), &visited) == combined.radius);
    assert(visited == 0);
    combined.RadiusAt(point, Receiver(0), &visited);
    assert(visited > 0 && visited < 200);
    const double small = combined.RadiusAt(point, Receiver(0));
    const double large = combined.RadiusAt(pov::Vector3d(0, 0.001, 0), Receiver(1));
    assert(std::abs(large / small - 20.0) < 1.0e-10);
    assert(small == separate.RadiusAt(point, Receiver(0)));
    const pov::Vector3d distant(100, 0, 100);
    visited = 0;
    assert(combined.RadiusAt(distant, Receiver(0), &visited) < 4.5 * small);
    assert(visited < 32);
    assert(std::abs(combined.RadiusAt(distant, Receiver(1)) / combined.RadiusAt(distant, Receiver(0)) - 20.0) < 1.0e-10);
    for (unsigned int pass : {0u, 3u, 63u, 1023u})
    {
        separate.Build(pass, 2);
        combined.Build(pass, 2);
        bright.Build(pass, 2);
        const double local = combined.RadiusAt(point, Receiver(0));
        assert(local == separate.RadiusAt(point, Receiver(0)));
        assert(local == bright.RadiusAt(point, Receiver(0)));
        const double scale = small * pov::ProgressivePhotonBudget::RadiusScale(pass, 2);
        assert(local >= scale && local <= 2.0 * scale);
        assert(std::abs(local / scale - 1.0) < 1.0e-12);
    }
    std::cout << "close receivers: local support ratio=" << large / small
              << "; unrelated receiver and emitted power preserve bandwidth\n";
}
