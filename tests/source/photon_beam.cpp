#include "core/lighting/photonbeam.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    double first, last;
    assert(pov::ClipPhotonBeam(2, 0, 1, 4, first, last));
    assert(first == 1 && last == 3);
    assert(pov::ClipPhotonBeam(0, 0, 1, 4, first, last));
    assert(first == 0 && last == 1);
    assert(!pov::ClipPhotonBeam(2, 1, 1, 4, first, last));
    assert(!pov::ClipPhotonBeam(-2, 0, 1, 4, first, last));
    for (double sigma : {0.0, 1e-12, 0.1, 4.0})
    {
        const double whole = pov::PhotonBeamIntegral(sigma, 0, 4);
        const double split = pov::PhotonBeamIntegral(sigma, 0, 1.3) +
            std::exp(-sigma * 1.3) * pov::PhotonBeamIntegral(sigma, 0, 2.7);
        assert(std::abs(whole - split) < 1e-12);
        double quadrature = 0;
        const int n = 100000;
        for (int i = 0; i < n; ++i)
            quadrature += std::exp(-sigma * (i + 0.5) * 4 / n) * 4 / n;
        assert(std::abs(whole - quadrature) < 1e-8);
    }
    // Parallel uniform beams integrate the spherical reconstruction kernel to unity.
    for (double radius : {0.1, 0.5, 1.0})
    {
        const int n = 100000;
        double sum = 0;
        for (int i = 0; i < n; ++i)
        {
            const double r2 = (i + 0.5) * radius * radius / n;
            assert(pov::ClipPhotonBeam(2, r2, radius, 4, first, last));
            sum += last - first;
        }
        const double density = sum / n * 3 / (4 * radius);
        assert(std::abs(density - 1) < 1e-7);
    }
    // A bent path is two segments; replacing it by its endpoint chord changes support.
    assert(pov::ClipPhotonBeam(1, 0, 0.1, 1, first, last));
    assert(!pov::ClipPhotonBeam(std::sqrt(0.5), 0.5, 0.1, std::sqrt(2.0), first, last));
    std::cout << "photon beams: clipping, extinction, subdivision, uniform kernel and bent path passed\n";
}
