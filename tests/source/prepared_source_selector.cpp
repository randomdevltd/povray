#include "core/material/preparedsourceselector.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
static PreparedSourceGeometry SphereSupport(DBL radius, Vector3d centre = Vector3d(0.0))
{
    PreparedSourceGeometry result;
    result.sphere = true;
    result.centre = centre;
    result.radius = radius;
    result.low = centre - Vector3d(radius);
    result.size = Vector3d(2 * radius);
    return result;
}
int main()
{
    const auto cubic = AutomaticSourceCartesianCounts(Vector3d(1.0));
    assert((cubic == std::array<size_t, 3>({80, 80, 80})));
    const auto thin = AutomaticSourceCartesianCounts(Vector3d(.01, 1, 1));
    assert(thin[X] == 2 && thin[Y] == 128 && thin[Z] == 128);
    assert(AutomaticSourceCartesianCounts(Vector3d(1e-100)) == cubic);
    assert(AutomaticSourceCartesianCounts(Vector3d(1e100)) == cubic);
    PreparedSourceDomainPolicy policy;
    policy.automaticCartesian = false;
    policy.cartesianCells = {{16, 16, 16}};
    policy.angularCells = 4;
    policy.radialCells = 2;
    const auto groups = PartitionSourceGeometry(
        {SphereSupport(10), SphereSupport(9), SphereSupport(1, Vector3d(1e-9, 0, 0)), SphereSupport(2)});
    assert(groups.size() == 2 && groups[0] == std::vector<size_t>({0, 1, 3}) &&
           groups[1] == std::vector<size_t>({2}));
    auto domain = SelectSourceDomain({SphereSupport(10), SphereSupport(9), SphereSupport(9.99)},
                                     {SphereSupport(8), SphereSupport(3, Vector3d(.001, 0, 0))}, policy);
    assert(domain->Spherical());
    const auto& radial = domain->RadialBreaks();
    for (DBL boundary : {0., .5, .8, .9, .999, 1.})
        assert(std::find(radial.begin(), radial.end(), boundary) != radial.end());
    assert(radial.size() == 6);
    assert(domain->SampleStencil(Vector3d(0.0)).valid);
    assert(domain->NodeCount() == (6 * 16 + 2) * 5 + 1);
    assert(std::fabs(domain->Volume() - 4 * std::acos(-1.0) * 1000 / 3) < 1e-9);
    auto differentCentre =
        SelectSourceDomain({SphereSupport(10), SphereSupport(1, Vector3d(1e-9, 0, 0))}, {}, policy);
    assert(!differentCentre->Spherical());
    auto nonSphere = SphereSupport(1);
    nonSphere.sphere = false;
    auto cartesian = SelectSourceDomain({SphereSupport(10), nonSphere}, {}, policy);
    assert(!cartesian->Spherical());
    assert(cartesian->Counts() == policy.cartesianCells);
    assert(cartesian->SampleStencil(Vector3d(-10, -10, -10)).valid);
    assert(cartesian->SampleStencil(Vector3d(10, 10, 10)).valid);
    auto refined = policy;
    refined.angularCells *= 2;
    refined.radialCells *= 2;
    auto fine = SelectSourceDomain({SphereSupport(10), SphereSupport(9)}, {SphereSupport(8)}, refined);
    assert(fine->NodeCount() > domain->NodeCount());
    for (DBL boundary : {.8, .9})
        assert(std::find(fine->RadialBreaks().begin(), fine->RadialBreaks().end(), boundary) !=
               fine->RadialBreaks().end());
    bool budget = false;
    policy.maxNodes = 10;
    try
    {
        SelectSourceDomain({SphereSupport(10)}, {}, policy);
    }
    catch (const std::invalid_argument&)
    {
        budget = true;
    }
    assert(budget);
    std::cout
        << "Prepared source selector exact geometry boundaries, independent topology and budgets pass\n";
}
