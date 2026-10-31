#include "core/lighting/bouncegrid.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
static void Near(const MathColour& a, const MathColour& b, DBL tolerance = 1e-5)
{
    for (int ch = 0; ch < MathColour::channels; ++ch)
        assert(std::fabs(a[ch] - b[ch]) < tolerance);
}
int main()
{
    const DBL fourPi = 4 * std::acos(-1.0);
    auto box = std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(Vector3d(0.0), Vector3d(2, 3, 4), {{2, 2, 2}}));
    std::vector<BounceFieldNode> nodes;
    for (const auto& point : box->Nodes())
    {
        const DBL amplitude = 1 + point[X] + 2 * point[Y] + 3 * point[Z];
        nodes.emplace_back();
        nodes.back().AddIsotropic(MathColour(.5 * amplitude));
        nodes.back().AddScatteringBeam(RAYLEIGH_SCATTERING, 0, MathColour(1), Vector3d(0, 0, 1),
                                       MathColour(amplitude * fourPi));
    }
    BounceFieldGrid field(box, std::move(nodes));
    assert(field.Domain() == box && field.StorageBytes() > 0);
    for (const auto& point : {Vector3d(.3, .4, .5), Vector3d(1, 1.5, 2), Vector3d(2, 3, 4)})
    {
        const DBL amplitude = 1 + point[X] + 2 * point[Y] + 3 * point[Z];
        Near(field.AngularAverage(point), MathColour(amplitude * 1.5));
        Near(field.Evaluate(point, Vector3d(0, 0, 1)), MathColour(amplitude * 2));
        Near(field.Evaluate(point, Vector3d(1, 0, 0)), MathColour(amplitude * 1.25));
        MathColour average;
        const size_t draws = 4096;
        for (size_t k = 0; k < draws; ++k)
        {
            const DBL mu = 2 * (k + .5) / draws - 1;
            average += field.Evaluate(point, Vector3d(std::sqrt(1 - mu * mu), 0, mu)) / draws;
        }
        Near(average, field.AngularAverage(point), 1e-4);
    }
    Near(field.Sample(Vector3d(-.01, 1, 1)), MathColour(0.0));
    bool rejected = false;
    try
    {
        BounceFieldGrid invalid(box, {});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    assert(rejected);
    std::cout << "Bounce source-domain interpolation and Rayleigh moment energy pass\n";
}
