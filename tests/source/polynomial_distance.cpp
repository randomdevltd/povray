#include "core/math/polynomialsolver.h"
#include "core/math/intersectiondepth.h"
#include "core/support/statistics.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace pov;

std::vector<double> coefficients(const std::vector<double>& roots)
{
    std::vector<double> result{1.0};
    for (double root : roots)
    {
        std::vector<double> next(result.size()+1,0.0);
        for (size_t i=0;i<result.size();++i)
        {
            next[i] += result[i];
            next[i+1] -= root*result[i];
        }
        result.swap(next);
    }
    return result;
}

void check(const std::vector<double>& c, std::vector<double> expected, double tolerance = 2e-9)
{
    RenderStatistics stats;
    double roots[MAX_ORDER];
    const int count = Solve_Polynomial(int(c.size())-1,c.data(),roots,true,0.0,stats);
    std::sort(roots,roots+count);
    std::sort(expected.begin(),expected.end());
    if (size_t(count) != expected.size())
        std::cerr << "order " << c.size()-1 << " expected " << expected.size() << " roots, got " << count << "\n";
    assert(size_t(count) == expected.size());
    for (int i=0;i<count;++i)
    {
        if (std::abs(roots[i]-expected[i]) > tolerance*std::max(1.0,std::abs(expected[i])))
            std::cerr << "order " << c.size()-1 << " root " << i << " expected " << expected[i] << " got " << roots[i] << "\n";
        assert(IsFiniteIntersectionDepth(roots[i]));
        assert(std::abs(roots[i]-expected[i]) <= tolerance*std::max(1.0,std::abs(expected[i])));
    }
}

void reject(const std::vector<double>& c, const char* reason)
{
    RenderStatistics stats;
    double roots[MAX_ORDER];
    bool rejected = false;
    try { Solve_Polynomial(int(c.size())-1,c.data(),roots,true,0.0,stats); }
    catch (const std::exception& error)
    {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
        if (!rejected) std::cerr << "unexpected rejection: " << error.what() << "\n";
    }
    assert(rejected);
}

int main()
{
    std::cerr.precision(17);
    check(coefficients({1,2,3}),{1,2,3});
    check(coefficients({0,1,2}),{0,1,2});
    check({1,0,0,0},{0});
    check(coefficients({-3,-2,-1}),{});
    check({1,0,2,0,1},{});
    check(coefficients({1,1,2,2}),{1,2},8*std::sqrt(std::numeric_limits<double>::epsilon()));
    check(coefficients({1,2,3,4,5,6,7}),{1,2,3,4,5,6,7});
    for (int order = 3; order <= MAX_ORDER; ++order)
    {
        std::vector<double> c(order+1,0.0);
        c.front() = 1.0; c.back() = -std::ldexp(1.0,order);
        check(c,{2.0});
    }
    auto tiny = coefficients({1,2,3});
    for (double& c : tiny) c *= 1e-9;
    check(tiny,{1,2,3});
    auto large = coefficients({1,2,3});
    for (double& c : large) c *= -1e200;
    check(large,{1,2,3});
    check({1e-12,1,-6,11,-6},{1,2,3});
    std::cout << "Sturm near, repeated, absent and scaled controls passed\n" << std::flush;
    for (double distance : {3.844e8,1.496e11})
    {
        check(coefficients({distance,2*distance,3*distance}),{distance,2*distance,3*distance});
        check(coefficients({1,2,distance}),{1,2,distance});
        std::cout << "Sturm roots through distance " << distance << " passed\n";
    }
    check({1e-9,0,0,-1e308},{std::pow(1e308,1.0/3.0)*1e3});
    std::vector<double> high(MAX_ORDER+1,0.0);
    high.front()=1.0; high.back()=-1e308;
    check(high,{std::pow(1e308,1.0/MAX_ORDER)});
    reject({1e-9,-1e-9,-1e307,1e307},"coefficient range");
    reject({1e100,0,0,-1e-300},"coefficient range");
    reject({1,1e200,-1e200,1},"coefficients exceed");
    reject({1,-1e308,1,1},"root bound");
    reject({1,0,0,std::numeric_limits<double>::infinity()},"must be finite");
    std::cout << "Sturm near, repeated, absent, scaled, distant and overflow controls passed\n";
}
