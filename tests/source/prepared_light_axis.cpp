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

static void CheckColourIntegral(bool tent = false)
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
    PreparedLightAxis axis(layout, field, masses, tent ? masses : std::vector<DBL>());
    const DBL volume = 24.0;
    DBL pdfIntegral = 0.0;
    for (size_t cell = 0; cell < centres.size(); ++cell)
    {
        const DBL floor = tent ? 1.0/1048576.0 : 0.5;
        const DBL expectedPdf = (floor + (1.0 - floor)*masses[cell]*centres.size()/totalMass)/volume;
        Near(axis.PositionPdf(centres[cell]), expectedPdf);
        assert(expectedPdf >= (tent ? 1.0/1048576.0 : 0.5)/volume);
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
        assert(sample.pdf >= (tent ? 1.0/1048576.0 : 0.5)/volume);
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

static void CheckTentProposal()
{
    for (const auto counts : {std::array<size_t, 3>{{7, 1, 1}}, {{1, 7, 1}}, {{1, 1, 7}}, {{1, 1, 1}}, {{3, 2, 4}}})
    {
        auto layout = std::make_shared<const PreparedGridLayout>(Vector3d(1048576.0, -2097152.0, 4194304.0),
            Vector3d(2.0, 3.0, 4.0), counts);
        const size_t n = layout->ValueCount();
        std::vector<DBL> masses(n, 1.0e-250), masked(n, 0.0);
        masses[n / 2] = 1.0e250;
        masked.back() = 1.0;
        std::vector<MathColour> values;
        for (DBL mass : masses)
            values.emplace_back(mass / 1.0e250);
        auto field = std::make_shared<const PreparedColourGrid>(layout, values);
        PreparedLightAxis axis(layout, field, masked, masses);
        DBL integral = 0.0;
        Vector3d expected(0.0), sampled(0.0);
        const size_t nx = 8*counts[X], ny = 8*counts[Y], nz = 8*counts[Z];
        for (size_t z = 0; z < nz; ++z)
            for (size_t y = 0; y < ny; ++y)
                for (size_t x = 0; x < nx; ++x)
                {
                    const Vector3d unit((x + 0.5)/nx, (y + 0.5)/ny, (z + 0.5)/nz);
                    const Vector3d point = layout->low + Vector3d(unit[X]*2, unit[Y]*3, unit[Z]*4);
                    const DBL weight = axis.PositionPdf(point)*24.0/(nx*ny*nz);
                    integral += weight;
                    expected += unit*weight;
                }
        Near(integral, 1.0);
        const size_t draws = 131072;
        for (size_t i = 1; i <= draws; ++i)
        {
            const auto sample = axis.SamplePosition(RadicalInverse(i, 2), RadicalInverse(i, 3),
                RadicalInverse(i, 5), RadicalInverse(i, 7));
            assert(sample.pdf > 0.0 && std::isfinite(sample.pdf));
            assert(sample.pdf == axis.PositionPdf(sample.position));
            for (int a = X; a <= Z; ++a)
                sampled[a] += (sample.position[a] - layout->low[a])/layout->size[a]/draws;
        }
        for (int a = X; a <= Z; ++a)
            assert(std::fabs(sampled[a] - expected[a]) < 0.003);
        for (DBL u : {0.0, 0.5, std::nextafter(1.0, 0.0)})
            for (DBL choose : {0.0, 1.0/1048576.0, 0.25, std::nextafter(0.5, 0.0), 0.5, std::nextafter(1.0, 0.0)})
            {
                const auto sample = axis.SamplePosition(choose, u, u, u);
                assert(sample.pdf > 0.0 && sample.pdf == axis.PositionPdf(sample.position));
            }
        assert(axis.PositionPdf(layout->low) > 0.0);
        assert(axis.PositionPdf(layout->low + layout->size) > 0.0);
        PreparedLightAxis legacy(layout, field, masked);
        PreparedLightAxis constant(layout, field, masked, std::vector<DBL>(n, 3.0));
        PreparedLightAxis zero(layout, field, masked, std::vector<DBL>(n, 0.0));
        assert(legacy.NodalStorageBytes() == 0);
        assert(constant.NodalStorageBytes() == 0);
        assert(zero.NodalStorageBytes() == 0);
        assert(axis.NodalStorageBytes() == (n > 1 ? 2*n*sizeof(DBL) : 0));
        for (DBL choose : {0.0, 0.125, 0.499, 0.5, 0.75})
        {
            const auto old = legacy.SamplePosition(choose, 0.1, 0.2, 0.3);
            for (const auto* same : {&constant, &zero})
            {
                const auto sample = same->SamplePosition(choose, 0.1, 0.2, 0.3);
                assert(sample.pdf == old.pdf);
                for (int a = X; a <= Z; ++a)
                    assert(sample.position[a] == old.position[a]);
            }
            if (choose >= 0.5)
                for (int a = X; a <= Z; ++a)
                    assert(axis.SamplePosition(choose, 0.1, 0.2, 0.3).position[a] == old.position[a]);
        }
        PreparedLightAxis unmasked(layout, field, std::vector<DBL>(n, 0.0), masses);
        assert(unmasked.SamplePosition(0.75, 0.1, 0.2, 0.3).pdf > 0.0);
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
        rejected = false;
        try { PreparedLightAxis bad(layout, colour, {}, invalid); }
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
    auto trailingLayout = std::make_shared<const PreparedGridLayout>(Vector3d(0.0), Vector3d(10.0, 1.0, 1.0),
        std::array<size_t, 3>{{10, 1, 1}});
    std::vector<DBL> trailingMasses(10, 1.0);
    trailingMasses.back() = 0.0;
    PreparedLightAxis trailing(trailingLayout, colour, trailingMasses);
    assert(trailing.PositionPdf(Vector3d(9.5, 0.5, 0.5)) == 0.05);
    assert(trailing.SamplePosition(std::nextafter(1.0, 0.0), 0.5, 0.5, 0.5).position[X] < 9.0);
    auto roundedLayout = std::make_shared<const PreparedGridLayout>(Vector3d(0.1, 0.2, 0.3),
        Vector3d(0.2, 0.3, 0.4), std::array<size_t, 3>{{3, 1, 1}});
    auto roundedField = std::make_shared<const PreparedColourGrid>(roundedLayout, std::vector<MathColour>(3, MathColour(1.0)));
    PreparedLightAxis rounded(roundedLayout, roundedField, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0});
    const DBL lastVariate = std::nextafter(1.0, 0.0);
    for (DBL choose : {std::nextafter(1.0/1048576.0, 0.0), std::nextafter(0.5, 0.0), lastVariate})
    {
        const auto sample = rounded.SamplePosition(choose, lastVariate, lastVariate, lastVariate);
        assert(sample.pdf > 0.0 && sample.pdf == rounded.PositionPdf(sample.position));
        for (int a = X; a <= Z; ++a)
            assert(sample.position[a] - roundedLayout->low[a] <= roundedLayout->size[a]);
        assert(rounded.ColourAt(sample.position).Weight() > 0.0);
    }
    CheckColourIntegral();
    CheckColourIntegral(true);
    CheckTentProposal();
    std::cout << "prepared light axis checks passed\n";
}
