#include "core/lighting/bouncegrid.h"
#include "core/lighting/sharedincidentfield.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;
static void Near(const MathColour& a, const MathColour& b, DBL tolerance = 2e-5)
{
    for (int ch = 0; ch < MathColour::channels; ++ch)
        assert(std::fabs(a[ch] - b[ch]) < tolerance);
}
int main()
{
    auto domain = std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(Vector3d(0.0), Vector3d(1.0), {{3, 3, 3}}));
    MathColour coefficient(1.0);
    coefficient[0] = .5;
    coefficient[1] = 2;
    coefficient[2] = 4;
    for (int type : {RAYLEIGH_SCATTERING, HENYEY_GREENSTEIN_SCATTERING})
    {
        std::vector<BounceFieldNode> incidentNodes, directNodes;
        for (size_t node = 0; node < domain->NodeCount(); ++node)
        {
            incidentNodes.emplace_back(16, node + 123);
            directNodes.emplace_back(16, node + 123);
            const auto& point = domain->Node(node);
            for (const auto& direction : {Vector3d(0, 0, 1), Vector3d(1, 0, 0), Vector3d(0, -1, 0)})
            {
                const MathColour weight(1 + point[X] + 2 * point[Y] + 3 * point[Z]);
                incidentNodes.back().AddScatteringBeam(type, .7, MathColour(1.0), direction, weight);
                directNodes.back().AddScatteringBeam(type, .7, coefficient, direction, weight);
            }
        }
        auto incident = std::make_shared<const BounceFieldGrid>(domain, std::move(incidentNodes));
        auto constant = std::make_shared<const PreparedConstantColourField>(coefficient);
        auto owner = std::make_shared<const PreparedOwnerBounceField>(
            domain, std::vector<PreparedOwnerBounceField::Component>{{incident, constant}});
        BounceFieldGrid direct(domain, std::move(directNodes));
        for (const auto& point : {Vector3d(.1, .2, .3), Vector3d(.5, .5, .5), Vector3d(.9, .6, .4)})
        {
            Near(owner->AngularAverage(point), direct.AngularAverage(point));
            for (const auto& direction : {Vector3d(0, 0, 1), Vector3d(1, 0, 0), Vector3d(0, -1, 0)})
                Near(owner->Evaluate(point, direction), direct.Evaluate(point, direction));
            MathColour integrated;
            for (size_t k = 0; k < 32768; ++k)
            {
                const DBL mu = 2 * (k + .5) / 32768 - 1, phi = k * 2.399963229728653;
                integrated += owner->Evaluate(point, Vector3d(std::sqrt(1 - mu * mu) * std::cos(phi),
                                                              std::sqrt(1 - mu * mu) * std::sin(phi), mu)) /
                              32768;
            }
            Near(integrated, owner->AngularAverage(point), .003);
        }
        std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> orders{owner, owner};
        SumPreparedBounceField sum(domain, std::move(orders));
        Near(sum.AngularAverage(Vector3d(.4)), owner->AngularAverage(Vector3d(.4)) * 2);
        size_t sharedBytes = incident->StorageBytes();
        for (size_t i = 0; i < 64; ++i)
        {
            PreparedOwnerBounceField another(domain, {{incident, constant}});
            sharedBytes += another.StorageBytes();
        }
        assert(sharedBytes < 64 * direct.StorageBytes() / 4);
    }
    std::cout << "Shared incident phase response equals owner scattering, preserves energy and scales "
                 "across64owners\n";
}
