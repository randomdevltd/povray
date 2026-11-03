#include "core/material/preparedlightaxis.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
using pov_base::MathColour;
static void Near(DBL a, DBL b, DBL tolerance = 1e-6)
{
    assert(std::fabs(a - b) <= tolerance * std::max(DBL(1), std::fabs(b)));
}
static std::shared_ptr<const PreparedSourceColourField> Field(std::shared_ptr<const PreparedSourceDomain> domain)
{
    std::vector<MathColour> values;
    for (size_t node = 0; node < domain->NodeCount(); ++node)
    {
        const Vector3d point = domain->Node(node);
        values.emplace_back(2 + point[X] * point[X]);
    }
    return std::make_shared<PreparedSourceColourField>(domain, std::move(values));
}
int main()
{
    auto box = std::make_shared<PreparedSourceDomain>(PreparedSourceDomain::Cartesian(Vector3d(0.0), Vector3d(1.0), {{2, 2, 2}}));
    std::vector<MathColour> affine;
    for (size_t node = 0; node < box->NodeCount(); ++node)
    {
        const Vector3d point = box->Node(node);
        affine.emplace_back(1 + point[X] + 2 * point[Y] + 3 * point[Z]);
    }
    PreparedSourceColourField linear(box, std::move(affine));
    Near(linear.Sample(Vector3d(.31, .42, .53))[0], 1 + .31 + 2 * .42 + 3 * .53);
    auto constant = std::make_shared<PreparedConstantColourField>(MathColour(3.0));
    PreparedLightAxis uniform(box, constant);
    for (std::uint64_t key = 0; key < 4096; ++key)
    {
        const auto sample = uniform.SamplePosition(key);
        Near(sample.pdf, 1);
        Near(uniform.PositionPdf(sample.position), sample.pdf);
        Near(uniform.ColourAt(sample.position)[0] / sample.pdf, 3);
    }
    std::vector<DBL> masses(box->CellCount(), 0);
    masses.back() = 1;
    PreparedLightAxis importance(box, constant, masses);
    DBL probability = 0;
    for (size_t cell = 0; cell < box->CellCount(); ++cell)
    {
        const DBL pdf = importance.PositionPdf(box->CellMidpoint(cell));
        assert(pdf > 0);
        probability += pdf * box->CellVolume(cell);
    }
    Near(probability, 1);
    PreparedLightAxis shared(box, std::make_shared<PreparedConstantColourField>(MathColour(7.0)), importance.Proposal());
    assert(shared.Proposal() == importance.Proposal());
    for (std::uint64_t key = 0; key < 32; ++key)
    {
        const auto a = importance.SamplePosition(key), b = shared.SamplePosition(key);
        Near((a.position - b.position).length(), 0);
        Near(a.pdf, b.pdf);
        Near(shared.ColourAt(b.position)[0], 7);
    }

    Near(importance.PositionPdf(Vector3d(-1, 0, 0)), 0);
    for (std::uint64_t key = 0; key < 4096; ++key)
    {
        const auto sample = importance.SamplePosition(key);
        Near(sample.pdf, importance.PositionPdf(sample.position));
    }
    DBL previous = 1;
    for (size_t count : {2, 4, 8, 16})
    {
        auto domain = std::make_shared<PreparedSourceDomain>(PreparedSourceDomain::Cartesian(Vector3d(0.0), Vector3d(1.0), {{count, count, count}}));
        const auto field = Field(domain);
        const Vector3d point(.371, .213, .619);
        const DBL error = std::fabs(field->Sample(point)[0] - (2 + point[X] * point[X]));
        assert(error < previous);
        previous = error;
    }
    for (DBL scale : {1e-8, 1.0, 6.4e6})
    {
        auto domain = std::make_shared<PreparedSourceDomain>(PreparedSourceDomain::Cartesian(Vector3d(-scale), Vector3d(2 * scale), {{3, 3, 3}}));
        std::vector<MathColour> values(domain->NodeCount(), MathColour(2.0));
        auto field = std::make_shared<PreparedSourceColourField>(domain, values);
        PreparedLightAxis scaled(domain, field);
        for (std::uint64_t key = 0; key < 4096; ++key)
        {
            const auto sample = scaled.SamplePosition(key);
            Near(sample.pdf * domain->Volume(), 1);
            Near(scaled.ColourAt(sample.position)[0], 2);
            Near(scaled.ColourAt(sample.position)[0] / sample.pdf / domain->Volume(), 2);
        }
        Near(field->Sample(Vector3d(scale * 1.01, 0, 0))[0], 0);
    }
    bool rejected = false;
    try { PreparedSourceColourField bad(box, std::vector<MathColour>(box->NodeCount(), MathColour(-1.0))); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { PreparedLightAxis bad(box, constant, {-1}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "Prepared source axis tests passed\n";
}
