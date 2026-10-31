#include "core/material/preparedsourceselector.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
static PreparedSourceGeometry Support(DBL radius, Vector3d centre = Vector3d(0.0))
{
    PreparedSourceGeometry result;
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
    auto joint = SelectSourceDomain({Support(10), Support(1, Vector3d(1e-9, 0, 0))}, policy);
    assert(joint->Counts() == policy.cartesianCells);
    assert(joint->NodeCount() == 17 * 17 * 17);
    assert(std::fabs(joint->Volume() - 8000) < 1e-9);
    assert(joint->SampleStencil(Vector3d(-10, -10, -10)).valid);
    assert(joint->SampleStencil(Vector3d(10, 10, 10)).valid);
    assert(!joint->SampleStencil(Vector3d(10.5, 0, 0)).valid);
    const auto farCentre = SelectSourceDomain({Support(1, Vector3d(1e9, 0, 0))}, policy);
    assert(farCentre->SampleStencil(Vector3d(1e9, 0, 0)).valid);
    bool budget = false;
    policy.maxNodes = 10;
    try
    {
        SelectSourceDomain({Support(10)}, policy);
    }
    catch (const std::invalid_argument&)
    {
        budget = true;
    }
    assert(budget);
    std::cout << "Prepared source selector lattice and budget pass\n";
}
