#include "core/material/preparedrefractionaxis.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;
using pov_base::MathColour;

class AffineField final : public PreparedScalarField
{
    public:
        AffineField(DBL intercept, Vector3d slope = Vector3d(0.0)) : intercept(intercept), slope(slope) {}
        PreparedScalarSample Sample(const Vector3d& point) const override
        {
            return PreparedScalarSample(intercept + slope[X]*point[X] + slope[Y]*point[Y] + slope[Z]*point[Z], slope);
        }
    private:
        const DBL intercept;
        const Vector3d slope;
};

static void Near(DBL actual, DBL expected, DBL tolerance = 1e-10)
{
    assert(std::fabs(actual - expected) < tolerance);
}

static void CheckGradient(const PreparedRefractionAxis& axis, const Vector3d& point)
{
    const auto sample = axis.Evaluate(point);
    for (int i = X; i <= Z; ++i)
    {
        Vector3d delta(0.0);
        delta[i] = 1e-5;
        Near(sample.gradient[i], (axis.Evaluate(point + delta).value - axis.Evaluate(point - delta).value) / 2e-5, 1e-8);
    }
}

int main()
{
    using Op = PreparedRefractionOperation;
    auto constant = [](DBL value) { return std::make_shared<const AffineField>(value); };
    auto linear = std::make_shared<const AffineField>(1.5, Vector3d(0.2, -0.3, 0.4));
    const Vector3d point(0.25, -0.5, 0.75);
    Near(PreparedRefractionAxis({}).Evaluate(point).value, 0.0);
    Near(PreparedRefractionAxis({{constant(-0.5), 1.0, Op::Add}}).Evaluate(point).value, -0.5);
    Near(PreparedRefractionAxis({{constant(1.5), 1.0, Op::Add}}).Evaluate(point).value, 1.5);
    PreparedRefractionAxis product({{linear, -2.0, Op::Add}, {linear, 999.0, Op::Multiply}});
    Near(product.Evaluate(point).value, -2.0 * std::pow(linear->Sample(point).value, 2));
    CheckGradient(product, point);
    PreparedRefractionAxis replace({{linear, 9.0, Op::Add}, {constant(-0.5), 2.0, Op::Replace}, {linear, 0.25, Op::Add}});
    Near(replace.Evaluate(point).value, -1.0 + 0.25 * linear->Sample(point).value);
    CheckGradient(replace, point);
    for (DBL sign : {-1.0, 1.0})
    {
        PreparedRefractionAxis subtract({{linear, sign * 3.0, Op::Add}, {linear, -0.5, Op::Subtract}});
        Near(subtract.Evaluate(point).value, sign * 2.5 * linear->Sample(point).value);
        CheckGradient(subtract, point);
        PreparedRefractionAxis clamp({{constant(sign), 1.0, Op::Add}, {linear, 3.0, Op::Subtract}});
        Near(clamp.Evaluate(point).value, 0.0);
        CheckGradient(clamp, point);
        PreparedRefractionAxis negativeAmount({{linear, sign, Op::Add}, {constant(-0.5), 2.0, Op::Subtract}});
        Near(negativeAmount.Evaluate(point).value, sign * (linear->Sample(point).value + 1.0));
        CheckGradient(negativeAmount, point);
    }
    auto other = std::make_shared<const AffineField>(0.25, Vector3d(-0.4, 0.1, 0.2));
    for (DBL sign : {-1.0, 1.0})
    {
        PreparedRefractionAxis mixed({{linear, sign * 3.0, Op::Add}, {other, 0.5, Op::Subtract},
                                     {other, 100.0, Op::Multiply}, {linear, 0.2, Op::Add}});
        const DBL a = linear->Sample(point).value, b = other->Sample(point).value;
        Near(mixed.Evaluate(point).value, sign * (3.0*a - 0.5*b)*b + 0.2*a);
        CheckGradient(mixed, point);
        CheckGradient(mixed, Vector3d(-0.5, 0.2, 0.1));
    }
    PreparedRefractionAxis zero({{linear, 1.0, Op::Add}, {linear, -1.0, Op::Add}, {linear, 0.5, Op::Subtract}});
    Near(zero.Evaluate(point).value, 0.0);
    CheckGradient(zero, point);
    PreparedConstantScalarField rawConstant(-0.5);
    Near(rawConstant.Sample(point).value, -0.5);
    Near(rawConstant.Sample(point).gradient[X], 0.0);
    auto layout = std::make_shared<const PreparedGridLayout>(Vector3d(-2.0, -3.0, -4.0),
                                                            Vector3d(4.0, 6.0, 8.0),
                                                            std::array<size_t, 3>{{4, 3, 4}});
    std::vector<DBL> values;
    for (size_t z = 0; z != 4; ++z)
        for (size_t y = 0; y != 3; ++y)
            for (size_t x = 0; x != 4; ++x)
                values.push_back(-0.5 + 0.2*(-2.0 + (x+0.5)) - 0.3*(-3.0 + (y+0.5)*2.0) +
                                 0.4*(-4.0 + (z+0.5)*2.0));
    auto grid = std::make_shared<const PreparedScalarGrid>(layout, values);
    Near(grid->Sample(point).value, -0.5 + 0.2*point[X] - 0.3*point[Y] + 0.4*point[Z]);
    Near(grid->Sample(point).gradient[X], 0.2);
    Near(grid->Sample(point).gradient[Y], -0.3);
    Near(grid->Sample(point).gradient[Z], 0.4);
    CheckGradient(PreparedRefractionAxis({{grid, 1.0, Op::Add}}), point);
    Near(grid->Sample(Vector3d(-1.9, 0.0, 0.0)).gradient[X], 0.0);
    Near(grid->Sample(Vector3d(2.01, 0.0, 0.0)).value, 0.0);
    Near(grid->Sample(Vector3d(2.01, 0.0, 0.0)).gradient[Y], 0.0);
    auto flatLayout = std::make_shared<const PreparedGridLayout>(Vector3d(0.0), Vector3d(2.0),
                                                                std::array<size_t, 3>{{1, 2, 1}});
    PreparedScalarGrid flat(flatLayout, {-0.5, 1.5});
    Near(flat.Sample(Vector3d(1.0)).value, 0.5);
    Near(flat.Sample(Vector3d(1.0)).gradient[X], 0.0);
    Near(flat.Sample(Vector3d(1.0)).gradient[Y], 2.0);
    Near(flat.Sample(Vector3d(1.0)).gradient[Z], 0.0);
    PreparedScalarGrid constantGrid(layout, std::vector<DBL>(layout->ValueCount(), 1.5));
    Near(constantGrid.Sample(point).value, 1.5);
    Near(constantGrid.Sample(point).gradient[X], 0.0);
    std::array<Vector3d, 3> rows{{Vector3d(0.5, 0.2, 0.0), Vector3d(0.0, 2.0, 0.1), Vector3d(0.0, 0.0, 0.25)}};
    auto transformed = std::make_shared<const TransformedPreparedScalarField>(grid, rows, Vector3d(0.0));
    const auto transformedSample = transformed->Sample(point);
    Near(transformedSample.gradient[X], 0.1);
    Near(transformedSample.gradient[Y], 0.04 - 0.6);
    Near(transformedSample.gradient[Z], -0.03 + 0.1);
    CheckGradient(PreparedRefractionAxis({{transformed, 1.0, Op::Add}}), point);
    bool layoutRejected = false;
    try { PreparedGridLayout invalid(Vector3d(0.0), Vector3d(1.0), {{0, 1, 1}}); }
    catch (const std::invalid_argument&) { layoutRejected = true; }
    assert(layoutRejected);
    bool sizeRejected = false;
    try { PreparedScalarGrid invalid(layout, {}); }
    catch (const std::invalid_argument&) { sizeRejected = true; }
    assert(sizeRejected);
    std::vector<MathColour> colourValues;
    for (size_t z = 0; z != 4; ++z)
        for (size_t y = 0; y != 3; ++y)
            for (size_t x = 0; x != 4; ++x)
            {
                MathColour colour;
                colour[0] = -2.0 + (x+0.5);
                colour[1] = 2.0*(-3.0 + (y+0.5)*2.0);
                colour[2] = -0.5*(-4.0 + (z+0.5)*2.0) + 1.5;
                colourValues.push_back(colour);
            }
    PreparedColourGrid colourGrid(layout, colourValues);
    const MathColour rgb = colourGrid.Sample(point);
    Near(rgb[0], point[X], 1e-6);
    Near(rgb[1], 2.0*point[Y], 1e-6);
    Near(rgb[2], -0.5*point[Z] + 1.5, 1e-6);
    for (int channel = 0; channel < MathColour::channels; ++channel)
        Near(colourGrid.Sample(Vector3d(2.01, 0.0, 0.0))[channel], 0.0);
    Near(colourGrid.Sample(Vector3d(-1.9, 0.0, 0.0))[0], -1.5);
    PreparedConstantColourField colourConstant(rgb);
    for (int channel = 0; channel < MathColour::channels; ++channel)
        Near(colourConstant.Sample(Vector3d(1e6))[channel], rgb[channel]);
    PreparedColourGrid colourFlat(flatLayout, {MathColour(-0.5), MathColour(1.5)});
    for (int channel = 0; channel < MathColour::channels; ++channel)
        Near(colourFlat.Sample(Vector3d(1.0))[channel], 0.5);
    bool colourSizeRejected = false;
    try { PreparedColourGrid invalid(layout, {}); }
    catch (const std::invalid_argument&) { colourSizeRejected = true; }
    assert(colourSizeRejected);
    bool rejected = false;
    try { PreparedRefractionAxis invalid({{nullptr, 1.0, Op::Add}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "prepared refraction axis checks passed\n";
}
