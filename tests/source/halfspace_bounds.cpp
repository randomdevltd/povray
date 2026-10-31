#include "core/math/halfspacebounds.h"
#include <cassert>
#include <ctime>
#include <iostream>

using namespace pov::halfspace_bounds;

static Plane Halfspace(double x, double y, double z, double distance)
{
    return Plane{{{Interval(x), Interval(y), Interval(z)}}, Interval(distance + 1e-10), Interval(distance)};
}

static std::vector<Plane> Box()
{
    return {Halfspace(1, 0, 0, 1), Halfspace(-1, 0, 0, 1), Halfspace(0, 1, 0, 1),
            Halfspace(0, -1, 0, 1), Halfspace(0, 0, 1, 1), Halfspace(0, 0, -1, 1)};
}

int main()
{
    std::array<double, 3> low, high;
    assert(Bounds(Box(), low, high));
    for (int axis = 0; axis < 3; ++axis)
        assert(low[axis] <= -1 && low[axis] > -1.000001 && high[axis] >= 1 && high[axis] < 1.000001);
    std::vector<Plane> prism;
    for (int angle = 0; angle < 360; angle += 60)
    {
        const double radians = angle * std::acos(-1.0) / 180;
        prism.push_back(Halfspace(std::cos(radians), 0, std::sin(radians), 0.6));
    }
    prism.push_back(Halfspace(0, 1, 0, 1.9));
    prism.push_back(Halfspace(0, -1, 0, 1.9));
    assert(Bounds(prism, low, high));
    assert(low[0] <= -0.6 && high[0] >= 0.6);
    assert(low[2] <= -0.6 / std::sin(std::acos(-1.0) / 3) && high[2] >= 0.6 / std::sin(std::acos(-1.0) / 3));
    const auto basePrism = prism;
    for (int angle = 1; angle < 90; angle += 7)
    {
        const double a = angle * std::acos(-1.0) / 180, c = std::cos(a), s = std::sin(a);
        prism = basePrism;
        for (Plane& plane : prism)
        {
            const Vector normal = plane.normal;
            plane.normal[0] = Interval(c) * normal[0] - Interval(s) * normal[1];
            plane.normal[1] = Interval(s) * normal[0] + Interval(c) * normal[1];
            const Interval shift = plane.normal[0] * Interval(-2.4) + plane.normal[1] * Interval(1.9) + plane.normal[2] * Interval(0.8);
            plane.offset = plane.offset + shift;
            plane.interiorOffset = plane.interiorOffset + shift;
        }
        assert(Bounds(prism, low, high));
        for (int vertex = 0; vertex < 6; ++vertex)
            for (double y : {-1.9, 1.9})
            {
                const double a0 = (vertex * 60 + 30) * std::acos(-1.0) / 180;
                const double x = 0.6 * std::cos(a0) / std::cos(std::acos(-1.0) / 6);
                const std::array<double, 3> point = {{c * x - s * y - 2.4, s * x + c * y + 1.9,
                                                    0.6 * std::sin(a0) / std::cos(std::acos(-1.0) / 6) + 0.8}};
                for (int axis = 0; axis < 3; ++axis)
                    assert(low[axis] <= point[axis] && high[axis] >= point[axis]);
            }
    }
    auto open = Box();
    open.pop_back();
    assert(!Bounds(open, low, high));
    const std::vector<Plane> wedge = {Halfspace(-1, 0, 0, 0), Halfspace(0, -1, 0, 0), Halfspace(0, 0, -1, 0),
                                     Halfspace(-1, -1, 1, 1), Halfspace(-1, 1, -1, 1)};
    assert(!Bounds(wedge, low, high));
    auto empty = Box();
    empty.push_back(Halfspace(1, 0, 0, -2));
    assert(!Bounds(empty, low, high));
    auto flat = Box();
    flat[0] = Halfspace(1, 0, 0, 0);
    flat[1] = Halfspace(-1, 0, 0, 0);
    assert(!Bounds(flat, low, high));
    auto uncertain = Box();
    uncertain[0].normal[0] = Interval(-1e-15, 1e-15);
    assert(!Bounds(uncertain, low, high));
    auto invalid = Box();
    invalid[0].normal[0] = Interval(std::numeric_limits<double>::quiet_NaN());
    assert(!Bounds(invalid, low, high));
    invalid = Box();
    invalid[0].offset = Interval(std::numeric_limits<double>::infinity());
    assert(!Bounds(invalid, low, high));
    auto affine = Box();
    const std::array<Vector, 3> inverse = {{{{Interval(0.5), Interval(-1) / Interval(24), Interval(1) / Interval(24)}},
                                          {{Interval(0), Interval(1) / Interval(3), Interval(-1) / Interval(3)}},
                                          {{Interval(0), Interval(0), Interval(2)}}}};
    for (Plane& plane : affine)
    {
        const Vector normal = plane.normal;
        for (int axis = 0; axis < 3; ++axis)
            plane.normal[axis] = normal[0] * inverse[0][axis] + normal[1] * inverse[1][axis] + normal[2] * inverse[2][axis];
        const Interval shift = plane.normal[0] * Interval(1000) + plane.normal[1] * Interval(-2000) + plane.normal[2] * Interval(3000);
        plane.offset = plane.offset + shift;
        plane.interiorOffset = plane.interiorOffset + shift;
    }
    affine.push_back(Halfspace(1, 1, 1, 10000));
    assert(Bounds(affine, low, high));
    for (double u : {-1.0, 1.0})
        for (double v : {-1.0, 1.0})
            for (double w : {-1.0, 1.0})
            {
                const std::array<double, 3> point = {{1000 + 2 * u + 0.25 * v, -2000 + 3 * v + 0.5 * w, 3000 + 0.5 * w}};
                for (int axis = 0; axis < 3; ++axis)
                    assert(low[axis] <= point[axis] && high[axis] >= point[axis]);
            }
    const Interval tiny(std::numeric_limits<double>::min());
    const Interval product = tiny * tiny;
    assert(product.low < 0 && product.high >= std::numeric_limits<double>::min());
    const Interval subnormal(std::numeric_limits<double>::denorm_min());
    assert(subnormal.low < 0 && subnormal.high >= std::numeric_limits<double>::min());
    const Interval overflow = Interval(std::numeric_limits<double>::max()) * Interval(2);
    assert(!Finite(overflow * Interval(0)));
    for (int count : {6, 64, 256})
    {
        std::vector<Plane> dense;
        for (int face = 0; face < count - 2; ++face)
        {
            const double radians = face * 2 * std::acos(-1.0) / (count - 2);
            dense.push_back(Halfspace(std::cos(radians), 0, std::sin(radians), 1));
        }
        dense.push_back(Halfspace(0, 1, 0, 2));
        dense.push_back(Halfspace(0, -1, 0, 2));
        const int repeats = 100;
        const std::clock_t start = std::clock();
        for (int repeat = 0; repeat < repeats; ++repeat)
            assert(Bounds(dense, low, high));
        const double microseconds = double(std::clock() - start) * 1e6 / CLOCKS_PER_SEC / repeats;
        std::cout << count << " plane preparation: " << microseconds << " us/call\n";
    }
    std::cout << "halfspace bounds checks passed\n";
}
