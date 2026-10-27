// Experimental projected-silhouette emission model; excluded from the regression suite.
#include "core/lighting/emitterangular.h"
#include "core/lighting/photonaim.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

int main()
{
    const double pi = std::acos(-1.0);
    const unsigned int samples = 1u << 20;
    std::mt19937_64 aiming(19);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<pov::PhotonAimBound> angularBounds(2);
    angularBounds[0].sphereCentre = pov::Vector3d(0.0);
    angularBounds[0].sphereRadius = 10.0;
    angularBounds[0].low = pov::Vector3d(-10.0);
    angularBounds[0].high = pov::Vector3d(10.0);
    angularBounds[0].weight = 1.0;
    angularBounds[0].target = 0;
    angularBounds[1].low = pov::Vector3d(3.0, -1.0, -1.0);
    angularBounds[1].high = pov::Vector3d(4.0, 1.0, 1.0);
    angularBounds[1].weight = 3.0;
    angularBounds[1].target = 1;
    pov::PhotonAimDomain angularProposal;
    angularProposal.BuildCap(pov::Vector3d(0.0), 0.0, angularBounds, nullptr, nullptr, 0.0);
    for (bool tube : {false, true})
    {
        const double radius = 0.05, length = 2.0;
        pov::EmitterAngularField field;
        assert(field.Build(64, 128, [&](const pov::Vector3d& d) {
            return tube ? pi * radius * radius * std::abs(d[pov::X]) +
                          2.0 * radius * length * std::sqrt(std::max(0.0, 1.0 - d[pov::X] * d[pov::X])) :
                          2.0 * std::abs(d[pov::Y]) + 0.04 * std::abs(d[pov::X]) + 0.08 * std::abs(d[pov::Z]);
        }));
        const double mean = tube ? pi * radius * (radius + length) / 2.0 : 1.06;
        const double moment = tube ? (pi * radius * radius / 4.0 + pi * radius * length / 8.0) / mean :
                                     (2.0 / 4.0 + 0.12 / 8.0) / mean;
        assert(std::abs(field.MeanArea() / mean - 1.0) < 0.002);
        double flux = 0.0, axial = 0.0;
        for (unsigned int i = 0; i < samples; ++i)
        {
            const auto pick = angularProposal.Sample(unit(aiming), unit(aiming));
            const auto direction = angularProposal.Direction(pick.x, pick.y);
            const double weight = field.Weight(direction) / (4.0 * pi * angularProposal.Pdf(pick.cell) * samples);
            flux += weight;
            axial += weight * direction[tube ? pov::X : pov::Y] * direction[tube ? pov::X : pov::Y];
        }
        std::cout << (tube ? "tube" : "panel") << " emitted angular flux " << flux << ", axial moment " << axial << "\n";
        assert(std::abs(flux - 1.0) < 0.01);
        assert(std::abs(axial / moment - 1.0) < 0.01);
    }
    return 0;
}
