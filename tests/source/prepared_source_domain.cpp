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
        auto sphere = PreparedSourceDomain::Sphere(Vector3d(0.0), scale, 4, {0, .2, .99, 1});
        Near(sphere.Volume() / (scale * scale * scale), 4 * std::acos(-1.0) / 3);
        assert(sphere.NodeCount() == (6 * 4 * 4 + 2) * 3 + 1);
        assert(!sphere.SampleStencil(Vector3d(1.001 * scale, 0, 0)).valid);
        assert(sphere.SampleStencil(Vector3d(0.0)).valid);
        DBL meanR3 = 0;
        size_t draws = 0;
        for (size_t cell = 0; cell < sphere.CellCount(); ++cell)
        {
            const auto mid = sphere.SampleStencil(sphere.CellMidpoint(cell));
            assert(mid.valid && mid.cell == cell);
            for (size_t k = 0; k < 128; ++k)
            {
                auto sample = sphere.SamplePosition(cell, cell * 8192 + k);
                Near(sample.pdf * sphere.CellVolume(cell), 1);
                Near(sphere.PositionPdf(cell, sample.position) / sample.pdf, 1);
                auto stencil = sphere.SampleStencil(sample.position);
                assert(stencil.valid);
                DBL sum = 0;
                for (auto w : stencil.weights)
                {
                    assert(w >= 0);
                    sum += w;
                }
                Near(sum, 1);
                if (cell / (4 * 4) % 3 == 2)
                {
                    const DBL r = sample.position.length() / scale;
                    meanR3 += r * r * r;
                    ++draws;
                }
            }
        }
        Near(meanR3 / draws, (1 + std::pow(.99, 3)) / 2, 1e-3);
        const DBL eps = 1e-9;
        for (Vector3d seam :
             {Vector3d(1, 1, .3), Vector3d(-1, 1, .3), Vector3d(.3, -1, -1), Vector3d(1, 1, 1)})
        {
            const Vector3d a = seam.normalized() * (.8 * scale);
            const Vector3d b = (seam + Vector3d(eps, 0, 0)).normalized() * (.8 * scale);
            Near(Field(sphere, a) / scale, Field(sphere, b) / scale, 1e-7);
        }
    }
    auto translated = PreparedSourceDomain::Sphere(Vector3d(1e9, -2e9, 3e9), 6.4e6, 3, {0, .99, .999999, 1});
    for (size_t cell = 0; cell < translated.CellCount(); ++cell)
        for (size_t k = 0; k < 64; ++k)
        {
            const auto sample = translated.SamplePosition(cell, cell * 1024 + k);
            Near(translated.PositionPdf(cell, sample.position) / sample.pdf, 1);
        }
    auto support = std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Sphere(Vector3d(0.0), 2, 3, {0, .2, .8, 1}));
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
        PreparedSourceDomain::Sphere(Vector3d(0.0), 2, 3, {0, .2, .8, 1}));
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
    Vector3d directionMoment(0.0), squaredMoment(0.0);
    DBL volumeEstimate = 0, polarCap = 0, fourthMoment = 0;
    for (size_t k = 0; k < 65536; ++k)
    {
        auto sample = uniform.SamplePosition(k + 12345);
        const auto direction = sample.position.normalized();
        directionMoment += direction / 65536;
        if (direction[X] > .9)
            polarCap += 1.0 / 65536;
        fourthMoment += std::pow(direction[X], 4) / 65536;
        for (int a = X; a <= Z; ++a)
            squaredMoment[a] += direction[a] * direction[a] / 65536;
        volumeEstimate += 1 / sample.pdf / 65536;
    }
    Near(volumeEstimate, uniformDomain->Volume(), 1e-9);
    Near(polarCap, .05, .003);
    Near(fourthMoment, .2, .004);
    for (int a = X; a <= Z; ++a)
    {
        Near(directionMoment[a], 0, .01);
        Near(squaredMoment[a], 1.0 / 3, .01);
    }
    bool collapsed = false;
    try
    {
        PreparedSourceDomain::Sphere(Vector3d(1e20), 1, 2, {0, .5, 1});
    }
    catch (const std::invalid_argument&)
    {
        collapsed = true;
    }
    assert(collapsed);
    bool rejected = false;
    try
    {
        PreparedSourceDomain::Sphere(Vector3d(0.0), 1, 2, {0, .4, .4, 1});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    assert(rejected);
    std::cout << "Prepared source domain volume, PDF, scale and seam checks passed\n";
}
