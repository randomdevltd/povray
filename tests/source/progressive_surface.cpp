#include "core/lighting/progressivephotonmap.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

namespace
{

struct Deposit final
{
    pov::Vector3d point, direction, normal;
    pov::MathColour flux;
};

double Estimate(const pov::ProgressivePhotonMap& map, const pov::Vector3d& point, const pov::Vector3d& normal)
{
    std::vector<pov::PhotonNeighbour> nearest;
    const double slab = pov::kPhotonSlab * map.BaseRadius(0);
    const double support = map.Nearest(point, normal, 0, map.Cap(0), nearest, [&](const pov::MappedPhoton& photon) {
        return pov::PhotonSurfaceMatch(photon, point, normal, slab);
    });
    double sum = 0.0;
    for (const auto& neighbour : nearest)
        sum += map.photons[neighbour.index].flux.Greyscale() * pov::PhotonKernel::Weight(neighbour.distance2, support * support);
    return sum * pov::PhotonKernel::Normaliser(support * support);
}

template<class Place>
double MeanRatio(unsigned int photons, unsigned int k, double area, Place&& place, unsigned int queries, std::uint64_t seed)
{
    std::mt19937_64 random(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const int owner = 0;
    pov::ProgressivePhotonMap map;
    for (unsigned int i = 0; i < photons; ++i)
    {
        Deposit deposit;
        place(unit(random), unit(random), deposit.point, deposit.normal);
        deposit.direction = deposit.normal;
        deposit.flux = pov::MathColour(1.0 / photons * (0.5 + unit(random)));
        map.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owner), 1.0);
    }
    map.Build(k);
    double total = 0.0;
    for (const auto& photon : map.photons)
        total += photon.flux.Greyscale();
    double sum = 0.0;
    for (unsigned int i = 0; i < queries; ++i)
    {
        pov::Vector3d point, normal;
        place(unit(random), unit(random), point, normal);
        sum += Estimate(map, point, normal);
    }
    return sum / queries / (total / area);
}

}

int main()
{
    std::mt19937_64 random(3);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double pi = std::acos(-1.0);
    const auto plane = [](double u, double v, pov::Vector3d& point, pov::Vector3d& normal) {
        point = pov::Vector3d(u * 10.0 - 5.0, 0.0, v * 10.0 - 5.0);
        normal = pov::Vector3d(0, 1, 0);
    };
    const auto interior = [](double u, double v, pov::Vector3d& point, pov::Vector3d& normal) {
        point = pov::Vector3d(u * 6.0 - 3.0, 0.0, v * 6.0 - 3.0);
        normal = pov::Vector3d(0, 1, 0);
    };
    std::mt19937_64 queries(11);
    double planeSum = 0.0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed)
    {
        const int owner = 0;
        pov::ProgressivePhotonMap map;
        std::mt19937_64 photons(seed);
        for (unsigned int i = 0; i < 100000; ++i)
        {
            Deposit deposit;
            plane(unit(photons), unit(photons), deposit.point, deposit.normal);
            deposit.direction = deposit.normal;
            deposit.flux = pov::MathColour(1.0e-5);
            map.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owner), 1.0);
        }
        map.Build(48);
        for (int i = 0; i < 500; ++i)
        {
            pov::Vector3d point, normal;
            interior(unit(queries), unit(queries), point, normal);
            planeSum += Estimate(map, point, normal);
        }
    }
    const double planeRatio = planeSum / 2000.0 / (1.0 / 100.0);
    std::cout << "uniform plane: mean estimate / analytic irradiance " << planeRatio << "\n";
    assert(std::abs(planeRatio - 1.0) < 0.01);

    const auto sphere = [pi](double u, double v, pov::Vector3d& point, pov::Vector3d& normal) {
        const double z = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - z * z)), phi = 2.0 * pi * v;
        normal = pov::Vector3d(r * std::cos(phi), r * std::sin(phi), z);
        point = normal * 2.0;
    };
    for (unsigned int photons : {200000u, 2000u})
    {
        double ratio = 0.0;
        for (std::uint64_t seed = 1; seed <= 4; ++seed)
            ratio += MeanRatio(photons, 48, 16.0 * pi, sphere, 4000, seed) / 4.0;
        std::cout << "uniform sphere, " << photons << " photons: mean estimate / analytic irradiance " << ratio << "\n";
        assert(std::abs(ratio - 1.0) < 0.01);
    }
    int owners[2];
    pov::ProgressivePhotonMap layered, bright;
    for (unsigned int i = 0; i < 40000; ++i)
    {
        Deposit deposit;
        plane(unit(random), unit(random), deposit.point, deposit.normal);
        deposit.direction = deposit.normal;
        deposit.flux = pov::MathColour(1.0e-4);
        layered.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[0]), 1.0);
        bright.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[0]), 2.0);
        if (i % 16 == 0)
        {
            deposit.point[pov::Y] = 0.001;
            layered.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[1]), 1.0);
            bright.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[1]), 2.0);
        }
    }
    layered.Build(48);
    bright.Build(48);
    const std::uint32_t dense = layered.Receiver(reinterpret_cast<pov::ConstObjectPtr>(&owners[0]));
    const std::uint32_t sparse = layered.Receiver(reinterpret_cast<pov::ConstObjectPtr>(&owners[1]));
    assert(dense != sparse && layered.Receiver(nullptr) == pov::ProgressivePhotonMap::noReceiver);
    const double ratio = layered.BaseRadius(sparse) / layered.BaseRadius(dense);
    assert(ratio > 3.0 && ratio < 5.0);
    assert(layered.BaseRadius(dense) == bright.BaseRadius(dense) && layered.BaseRadius(sparse) == bright.BaseRadius(sparse));
    std::cout << "close receivers: base radius ratio " << ratio << " for 1/16 the density; emitted power keeps the support\n";
    std::cout << "progressive photon density estimator passed\n";
    return 0;
}
