#include "core/material/physicalphase.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;

int main()
{
    const int count = 200000;
    for (int type = 1; type <= 5; ++type)
        for (double g : {-0.8, 0.0, 0.8})
        {
            double integral = 0.0, mean = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double mu = -1.0 + 2.0 * (i + 0.5) / count;
                const double mass = PhysicalPhaseFactor(type, g, mu) / count;
                integral += mass;
                mean += mu * mass;
            }
            assert(std::abs(integral - 1.0) < 1e-7);
            if (type == HENYEY_GREENSTEIN_SCATTERING)
                assert(std::abs(mean - g) < 1e-7);
        }
    assert(PhysicalPhaseFactor(HENYEY_GREENSTEIN_SCATTERING, 0.8, 1.0) >
           PhysicalPhaseFactor(HENYEY_GREENSTEIN_SCATTERING, 0.8, -1.0));
    assert(std::abs(PhysicalPhaseFactor(RAYLEIGH_SCATTERING, 0.0, 0.0) - 0.75) < 1e-15);
    std::cout << "normalized physical phases and HG first moment pass\n";
}
