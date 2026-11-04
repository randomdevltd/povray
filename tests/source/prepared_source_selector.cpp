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
    const Vector3d asymmetric(.67, 1, .28);
    const auto initial = AutomaticSourceCartesianCounts(asymmetric);
    assert((initial == std::array<size_t, 3>({86, 128, 36})));
    auto previous = initial;
    const auto defaultDomain = PreparedSourceDomain::Cartesian(Vector3d(0.), asymmetric, initial);
    DBL previousSpacing = defaultDomain.Node(1)[X];
    for (double cells : {2000000., 4000000., 8000000., 1.e9})
    {
        for (const Vector3d& extent : {asymmetric, Vector3d(.01, 1, 1), Vector3d(1.)})
        {
            const auto counts = AutomaticSourceCartesianCounts(extent, cells);
            size_t nodes = 1;
            for (size_t count : counts)
                nodes *= count + 1;
            assert(nodes <= AutomaticSourceNodeBound(cells));
            PreparedSourceDomainPolicy automatic;
            automatic.automaticCells = cells;
            assert(SelectSourceDomain({{Vector3d(0.), extent}}, automatic)->NodeCount() == nodes);
        }
        const auto counts = AutomaticSourceCartesianCounts(asymmetric, cells);
        for (int a = X; a <= Z; ++a)
            assert(counts[a] > previous[a]);
        const auto refined = PreparedSourceDomain::Cartesian(Vector3d(0.), asymmetric, counts);
        assert(refined.Node(1)[X] < previousSpacing);
        assert(refined.SampleStencil(Vector3d(.123, .456, .078)).valid);
        previousSpacing = refined.Node(1)[X];
        previous = counts;
    }
    for (double invalid : {0., -1., std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN(), 1.e300})
    {
        bool rejected = false;
        try { AutomaticSourceNodeBound(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
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
    const auto billion = AutomaticSourceCartesianCounts(Vector3d(1.0), raised.automaticCells);
    assert(billion[X] >= 1000 && billion[X] <= 1001);
    assert(billion[X] == billion[Y] && billion[Y] == billion[Z]);
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
