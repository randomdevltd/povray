#include "core/material/preparedsourcelayout.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
static void Near(DBL a, DBL b, DBL tolerance = 1e-10)
{
    assert(std::fabs(a - b) <= tolerance * std::max(DBL(1), std::fabs(b)));
}
static DBL Field(const PreparedSourceDomain& d, const Vector3d& p)
{
    auto s = d.SampleStencil(p);
    assert(s.valid);
    DBL result = 0, sum = 0;
    for (int i = 0; i < 8; ++i)
    {
        assert(s.weights[i] >= 0);
        sum += s.weights[i];
        const auto& n = d.Node(s.nodes[i]);
        result += s.weights[i] * (n[X] + 2 * n[Y] + 3 * n[Z]);
    }
    Near(sum, 1);
    return result;
}
int main()
{
    auto box = PreparedSourceDomain::Cartesian(Vector3d(2, 3, 4), Vector3d(2, 4, 8), {{2, 3, 4}});
    Near(box.Volume(), 64);
    Near(Field(box, Vector3d(2.8, 4.7, 6.2)), 2.8 + 2 * 4.7 + 3 * 6.2);
    assert(!box.SampleStencil(Vector3d(1.9, 4, 5)).valid);
    for (DBL scale : {1e-8, 1.0, 6.4e6})
    {
        auto cube = PreparedSourceDomain::Cartesian(Vector3d(-scale), Vector3d(2 * scale), {{4, 3, 5}});
        Near(cube.Volume() / (scale * scale * scale), 8);
        assert(cube.NodeCount() == 5 * 4 * 6);
        assert(!cube.SampleStencil(Vector3d(1.001 * scale, 0, 0)).valid);
        for (size_t cell = 0; cell < cube.CellCount(); ++cell)
        {
            const auto mid = cube.SampleStencil(cube.CellMidpoint(cell));
            assert(mid.valid && mid.cell == cell);
            for (size_t k = 0; k < 64; ++k)
            {
                auto sample = cube.SamplePosition(cell, cell * 8192 + k);
                Near(sample.pdf * cube.CellVolume(cell), 1);
                Near(cube.PositionPdf(cell, sample.position) / sample.pdf, 1);
                Near(Field(cube, sample.position) / scale, (sample.position[X] + 2 * sample.position[Y] +
                                                             3 * sample.position[Z]) / scale, 1e-9);
            }
        }
    }
    auto translated = PreparedSourceDomain::Cartesian(Vector3d(1e9, -2e9, 3e9), Vector3d(6.4e6), {{3, 3, 3}});
    for (size_t cell = 0; cell < translated.CellCount(); ++cell)
        for (size_t k = 0; k < 64; ++k)
        {
            const auto sample = translated.SamplePosition(cell, cell * 1024 + k);
            Near(translated.PositionPdf(cell, sample.position) / sample.pdf, 1);
        }
    // the lattice is implicit: whatever the cell count, the domain itself stores a fixed handful of bytes
    auto dense = PreparedSourceDomain::Cartesian(Vector3d(0.0), Vector3d(1.0), {{128, 128, 128}});
    assert(dense.CellCount() == 128 * 128 * 128);
    assert(dense.StorageBytes() == sizeof(PreparedSourceDomain));
    assert(translated.StorageBytes() == sizeof(PreparedSourceDomain));
    auto support = std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(Vector3d(-2), Vector3d(4), {{3, 3, 3}}));
    std::vector<DBL> masses(support->CellCount(), 0);
    masses.front() = 1e300;
    PreparedSourceDistribution distribution(support, masses);
    DBL integral = 0;
    for (size_t cell = 0; cell < support->CellCount(); ++cell)
    {
        const DBL density = distribution.PositionPdf(support->CellMidpoint(cell));
        assert(density > 0);
        integral += density * support->CellVolume(cell);
    }
    Near(integral, 1);
    size_t firstCell = 0;
    for (size_t k = 0; k < 10000; ++k)
    {
        auto sample = distribution.SamplePosition(k);
        if (support->SampleStencil(sample.position).cell == 0)
            ++firstCell;
        assert(sample.pdf > 0);
        Near(sample.pdf, distribution.PositionPdf(sample.position));
    }
    Near(DBL(firstCell) / 10000, .99 + .01 * support->CellVolume(0) / support->Volume(), .005);
    size_t bitDraws = 0;
    const long double rare = std::ldexp(1.0L, -80);
    assert(prepared_source_detail::Bernoulli(rare,
                                             [&bitDraws]
                                             {
                                                 ++bitDraws;
                                                 return UINT64_C(0);
                                             }));
    assert(bitDraws == 80);
    bitDraws = 0;
    assert(!prepared_source_detail::Bernoulli(rare, [&bitDraws]
                                              { return ++bitDraws == 40 ? UINT64_C(1) : UINT64_C(0); }));
    assert(bitDraws == 40);
    auto uniformDomain = std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(Vector3d(-2), Vector3d(4), {{3, 3, 3}}));
    PreparedSourceDistribution uniform(uniformDomain, std::vector<DBL>(uniformDomain->CellCount(), 0));
    for (size_t cell = 0; cell < uniformDomain->CellCount(); ++cell)
        Near(uniform.PositionPdf(uniformDomain->CellMidpoint(cell)) * uniformDomain->Volume(), 1);
    assert(uniform.StorageBytes() > 0 && uniformDomain->StorageBytes() > 0);
    PreparedSourceDistribution emptyUniform(uniformDomain, {});
    Near(emptyUniform.StorageBytes(), uniform.StorageBytes());
    for (size_t k = 0; k < 1000; ++k)
    {
        const auto sample = uniformDomain->SampleUniformPosition(k);
        Near(sample.pdf * uniformDomain->Volume(), 1);
        Near(emptyUniform.PositionPdf(sample.position) / sample.pdf, 1);
    }
    DBL volumeEstimate = 0;
    for (size_t k = 0; k < 65536; ++k)
        volumeEstimate += 1 / uniform.SamplePosition(k + 12345).pdf / 65536;
    Near(volumeEstimate, uniformDomain->Volume(), 1e-9);
    bool collapsed = false;
    try
    {
        PreparedSourceDomain::Cartesian(Vector3d(1e20), Vector3d(1), {{2, 2, 2}});
    }
    catch (const std::invalid_argument&)
    {
        collapsed = true;
    }
    assert(collapsed);
    std::cout << "Prepared source domain volume, PDF and scale checks passed\n";
}
