#include "core/material/preparedlightaxis.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace pov;
using pov_base::MathColour;
static void Near(DBL actual, DBL expected) { assert(std::fabs(actual - expected) < 1e-6); }
static DBL RadicalInverse(size_t index, size_t base)
{
    DBL result = 0.0, weight = 1.0;
    while (index)
    {
        weight /= base;
        result += (index % base) * weight;
        index /= base;
    }
    return result;
}

static void CheckColourIntegral()
{
    auto layout = std::make_shared<const PreparedGridLayout>(Vector3d(-1.0, 2.0, -2.0),
        Vector3d(2.0, 3.0, 4.0), std::array<size_t, 3>{{4, 3, 2}});
    std::vector<MathColour> values;
    std::vector<Vector3d> centres;
    for (size_t z = 0; z < 2; ++z)
        for (size_t y = 0; y < 3; ++y)
            for (size_t x = 0; x < 4; ++x)
            {
                const Vector3d unit((x + 0.5)/4.0, (y + 0.5)/3.0, (z + 0.5)/2.0);
                centres.push_back(layout->low + Vector3d(2*unit[X], 3*unit[Y], 4*unit[Z]));
                MathColour value(0.0);
                value[0] = 0.25 + unit[X];
                value[1] = 1.0 + 2.0*unit[Y];
                value[2] = 3.0 + 0.5*unit[Z];
                values.push_back(value);
            }
    auto field = std::make_shared<const PreparedColourGrid>(layout, values);
    std::vector<DBL> masses;
    DBL totalMass = 0.0;
    for (const auto& centre : centres)
    {
        const auto value = field->Sample(centre);
        const DBL mass = value[0] + value[1] + value[2];
        masses.push_back(mass);
        totalMass += mass;
    }
    PreparedLightAxis axis(layout, field, masses);
    const DBL volume = 24.0;
    DBL pdfIntegral = 0.0;
    for (size_t cell = 0; cell < centres.size(); ++cell)
    {
        const DBL expectedPdf = (0.5 + 0.5*masses[cell]*centres.size()/totalMass)/volume;
        Near(axis.PositionPdf(centres[cell]), expectedPdf);
        assert(expectedPdf >= 0.5/volume);
        pdfIntegral += axis.PositionPdf(centres[cell])*volume/centres.size();
    }
    Near(pdfIntegral, 1.0);
    assert(axis.PositionPdf(centres.front()) < axis.PositionPdf(centres.back()));
    Near(axis.PositionPdf(Vector3d(-1.01, 3.0, 0.0)), 0.0);
    // The three slopes and intercepts independently identify the RGB channels.
    const auto interior = axis.ColourAt(Vector3d(-0.2, 3.2, -0.4));
    Near(interior[0], 0.65);
    Near(interior[1], 1.8);
    Near(interior[2], 3.2);
    // Symmetric edge clamping preserves each affine channel's domain average.
    const std::array<DBL, 3> exact{{18.0, 48.0, 78.0}};
    std::array<DBL, 3> estimate{{0.0, 0.0, 0.0}};
    const size_t samples = 131072;
    const DBL relativeQuadratureBudget = 0.005;
    for (size_t i = 1; i <= samples; ++i)
    {
        const auto sample = axis.SamplePosition(RadicalInverse(i, 2), RadicalInverse(i, 3),
                                                RadicalInverse(i, 5), RadicalInverse(i, 7));
        Near(sample.pdf, axis.PositionPdf(sample.position));
        assert(sample.pdf >= 0.5/volume);
        const auto value = axis.ColourAt(sample.position);
        for (size_t channel = 0; channel < estimate.size(); ++channel)
            estimate[channel] += value[channel]/sample.pdf/samples;
    }
    for (size_t channel = 0; channel < estimate.size(); ++channel)
    {
        assert(std::fabs(estimate[channel] - exact[channel]) < relativeQuadratureBudget*exact[channel]);
        std::cout << "RGB integral " << channel << ": " << estimate[channel] << " expected " << exact[channel] << "\n";
    }
}

int main()
{
    auto layout = std::make_shared<const PreparedGridLayout>(Vector3d(0.0), Vector3d(2.0, 1.0, 1.0),
                                                            std::array<size_t, 3>{{2, 1, 1}});
    MathColour rgb;
    rgb[0] = 0.25; rgb[1] = 2.0; rgb[2] = 7.0;
    auto colour = std::make_shared<const PreparedConstantColourField>(rgb);
    PreparedLightAxis axis(layout, colour, {0.0, 3.0});
    Near(axis.PositionPdf(Vector3d(0.25, 0.5, 0.5)), 0.25);
    Near(axis.PositionPdf(Vector3d(1.25, 0.5, 0.5)), 0.75);
    Near(axis.PositionPdf(Vector3d(-0.01, 0.5, 0.5)), 0.0);
    int lowCount = 0;
    for (int i = 0; i != 10000; ++i)
    {
        const auto sample = axis.SamplePosition((i + 0.5)/10000.0, 0.3, 0.4, 0.6);
        assert(sample.position[X] >= 0.0 && sample.position[X] <= 2.0);
        Near(sample.pdf, axis.PositionPdf(sample.position));
        if (sample.position[X] < 1.0) ++lowCount;
        for (int channel = 0; channel != MathColour::channels; ++channel)
            Near(axis.ColourAt(sample.position)[channel], rgb[channel]);
    }
    assert(lowCount == 2500);
    PreparedLightAxis zero(layout, colour, {0.0, 0.0});
    PreparedLightAxis absent(layout, colour);
    Near(zero.SamplePosition(0.75, 0.1, 0.2, 0.3).position[X], 1.5);
    Near(absent.PositionPdf(Vector3d(0.5)), 0.5);
    Near(zero.PositionPdf(Vector3d(1.5, 0.5, 0.5)), 0.5);
    PreparedLightAxis unequal(layout, colour, {1.0, 3.0});
    Near(unequal.PositionPdf(Vector3d(0.5)), 0.375);
    Near(unequal.PositionPdf(Vector3d(1.5, 0.5, 0.5)), 0.625);
    Near(unequal.SamplePosition(0.6, 0.2, 0.3, 0.4).position[X], 0.2);
    Near(unequal.SamplePosition(0.8, 0.2, 0.3, 0.4).position[X], 1.2);
    for (std::vector<DBL> invalid : {std::vector<DBL>{-1.0, 1.0}, std::vector<DBL>{1.0},
                                    std::vector<DBL>{std::numeric_limits<DBL>::infinity(), 1.0},
                                    std::vector<DBL>{std::numeric_limits<DBL>::quiet_NaN(), 1.0}})
    {
        bool rejected = false;
        try { PreparedLightAxis bad(layout, colour, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    auto mortonLayout = std::make_shared<const PreparedGridLayout>(Vector3d(0.0), Vector3d(4.0, 2.0, 1.0),
                                                                  std::array<size_t, 3>{{4, 2, 1}});
    PreparedLightAxis morton(mortonLayout, colour, std::vector<DBL>(8, 1.0));
    const auto thirdMortonCell = morton.SamplePosition(0.65625, 0.25, 0.25, 0.25);
    Near(thirdMortonCell.position[X], 0.25);
    Near(thirdMortonCell.position[Y], 1.25);
    Near(thirdMortonCell.pdf, 0.125);
    CheckColourIntegral();
    std::cout << "prepared light axis checks passed\n";
}
