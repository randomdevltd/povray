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
    assert((AutomaticSourceCartesianCounts(Vector3d(1.0), 65536) == std::array<size_t, 3>({41, 41, 41})));
    assert((AutomaticSourceCartesianCounts(Vector3d(1.0), 100000) == std::array<size_t, 3>({47, 47, 47})));
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
    PreparedSourceDomainPolicy raised;
    raised.automaticCells = 1.0e9;
    assert(SelectSourceDomain({Support(1)}, raised)->NodeCount() == 129 * 129 * 129);
    // a light's own domain_cells overrides the scene budget; an unpinned light falls back to the scene's, then the default
    PreparedSourceDomainPolicy scene;
    scene.automaticCells = 65536;
    PreparedSourceDomainPolicy pinned = scene;
    pinned.automaticCells = 262144;
    assert((SelectSourceDomain({Support(1)}, pinned)->Counts() == std::array<size_t, 3>({65, 65, 65})));
    assert((SelectSourceDomain({Support(1)}, scene)->Counts() == std::array<size_t, 3>({41, 41, 41})));
    assert((SelectSourceDomain({Support(1)}, PreparedSourceDomainPolicy())->Counts() == std::array<size_t, 3>({80, 80, 80})));
    std::cout << "Prepared source selector lattice and budget pass\n";
}
