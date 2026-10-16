#include "core/lighting/emitter.h"
#include "core/lighting/progressivephotonmap.h"

#include <algorithm>
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

double Estimate(const pov::ProgressivePhotonMap& map, const pov::Vector3d& point, const pov::Vector3d& normal,
                std::uint32_t receiver = 0, double* used = nullptr)
{
    std::vector<pov::PhotonNeighbour> nearest;
    const double slab = pov::kPhotonSlab * map.BaseRadius(receiver);
    const double support = map.Nearest(point, normal, receiver, map.Cap(receiver), nearest, [&](const pov::MappedPhoton& photon) {
        return photon.receiver == receiver && pov::PhotonSurfaceMatch(photon, point, normal, slab);
    });
    if (used)
        *used = support;
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

    pov::ProgressivePhotonMap mixed;
    std::mt19937_64 spread(5);
    for (unsigned int i = 0; i < 60000 + 25600; ++i)
    {
        Deposit deposit;
        const bool dense = i < 60000;
        deposit.point = dense ? pov::Vector3d(unit(spread), 0.0, unit(spread)) : pov::Vector3d(1.0 + 8.0 * unit(spread), 0.0, 8.0 * unit(spread));
        deposit.normal = deposit.direction = pov::Vector3d(0, 1, 0);
        deposit.flux = pov::MathColour(1.0e-4);
        mixed.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[0]), 1.0);
    }
    mixed.Build(48);
    double capped = 0.0, sparseSum = 0.0;
    for (int i = 0; i < 4000; ++i)
    {
        double support = 0.0;
        sparseSum += Estimate(mixed, pov::Vector3d(1.5 + 7.0 * unit(queries), 0.0, 0.5 + 7.0 * unit(queries)), pov::Vector3d(0, 1, 0), 0, &support);
        capped += support == mixed.Cap(0);
    }
    const double sparseRatio = sparseSum / 4000.0 / (25600.0e-4 / 64.0);
    std::cout << "sparse region at the support cap: mean estimate / analytic irradiance " << sparseRatio << ", " << capped / 40.0 << "% capped\n";
    assert(capped > 3600.0 && std::abs(sparseRatio - 1.0) < 0.02);

    for (bool alone : {false, true})
    {
        pov::ProgressivePhotonMap faint;
        for (unsigned int i = 0; !alone && i < 40000; ++i)
        {
            Deposit deposit;
            plane(unit(random), unit(random), deposit.point, deposit.normal);
            deposit.direction = deposit.normal;
            deposit.flux = pov::MathColour(1.0e-4);
            faint.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[0]), 1.0);
        }
        for (unsigned int i = 0; i < 20; ++i)
        {
            Deposit deposit;
            deposit.point = pov::Vector3d(0.05 * unit(random), 0.002, 0.05 * unit(random));
            deposit.normal = deposit.direction = pov::Vector3d(0, 1, 0);
            deposit.flux = pov::MathColour(1.0e-3);
            faint.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[1]), 1.0);
        }
        faint.Build(48);
        const std::uint32_t id = faint.Receiver(reinterpret_cast<pov::ConstObjectPtr>(&owners[1]));
        const double lit = Estimate(faint, pov::Vector3d(0.025, 0.002, 0.025), pov::Vector3d(0, 1, 0), id);
        std::cout << "20-photon caustic" << (alone ? " alone" : " beside a dense receiver") << ": base radius " << faint.BaseRadius(id)
                  << ", estimate " << lit << "\n";
        assert(faint.BaseRadius(id) > 1.0e-4 && faint.BaseRadius(id) < 1.0 && lit > 0.0);
    }

    pov::ProgressivePhotonMap scattered;
    std::mt19937_64 brute(9);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (unsigned int i = 0; i < 6000; ++i)
    {
        Deposit deposit;
        deposit.point = pov::Vector3d(unit(brute), 0.2 * unit(brute), unit(brute));
        deposit.normal = pov::Vector3d(gauss(brute), gauss(brute), gauss(brute)).normalized();
        deposit.direction = deposit.normal;
        deposit.flux = pov::MathColour(1.0);
        scattered.Add(deposit, reinterpret_cast<pov::ConstObjectPtr>(&owners[i % 2]), 1.0);
    }
    scattered.Build(24);
    std::vector<pov::PhotonNeighbour> found;
    for (int i = 0; i < 400; ++i)
    {
        const pov::Vector3d point(unit(brute), 0.2 * unit(brute), unit(brute));
        const pov::Vector3d normal = pov::Vector3d(gauss(brute), gauss(brute), gauss(brute)).normalized();
        const std::uint32_t receiver = i % 3 == 2 ? pov::ProgressivePhotonMap::noReceiver : std::uint32_t(i % 3);
        const bool twoSided = i % 4 == 0;
        const double cap = 0.05 + 0.3 * unit(brute);
        auto accept = [&](const pov::MappedPhoton& photon) {
            const double agreement = dot(photon.Normal(), normal);
            return (receiver == pov::ProgressivePhotonMap::noReceiver || photon.receiver == receiver) &&
                   (agreement >= 0.5 || (twoSided && agreement <= -0.5));
        };
        const double support = scattered.Nearest(point, normal, receiver, cap, found, accept, twoSided);
        std::vector<double> all;
        for (const auto& photon : scattered.photons)
            if (accept(photon) && (photon.point - point).lengthSqr() <= cap * cap)
                all.push_back((photon.point - point).lengthSqr());
        std::sort(all.begin(), all.end());
        assert(found.size() == std::min<size_t>(24, all.size()));
        assert(support == (all.size() < 24 ? cap : std::sqrt(all[23])));
        std::vector<double> got;
        for (const auto& neighbour : found)
            got.push_back(neighbour.distance2);
        std::sort(got.begin(), got.end());
        assert(std::equal(got.begin(), got.end(), all.begin()));
    }
    std::cout << "partitioned nearest neighbours match a brute-force search\n";

    std::vector<pov::Vector3d> axes = {pov::Vector3d(0, 0, 1), pov::Vector3d(1, 0, 0), pov::Vector3d(0, -1, 0)};
    const std::vector<double> widths = {0.02, 0.3, 2.0}, chance = {0.5, 0.3, 0.2};
    std::mt19937_64 emission(13);
    const unsigned int samples = 1u << 21;
    double whole = 0.0, narrow = 0.0, outside = 0.0;
    for (unsigned int i = 0; i < samples; ++i)
    {
        const double u = unit(emission), v = unit(emission);
        const double z = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - z * z));
        pov::Vector3d direction(r * std::cos(2.0 * pi * v), r * std::sin(2.0 * pi * v), z);
        size_t selected;
        bool targeted;
        const double pdf = pov::PhotonTargetMixture(axes, widths, chance, u, v, unit(emission), unit(emission), direction, selected, targeted);
        const double weight = 1.0 / (samples * pdf);
        whole += weight;
        narrow += direction[pov::Z] >= 1.0 - widths[0] ? weight : 0.0;
        outside += direction[pov::Z] < 0.0 && direction[pov::X] < 0.0 ? weight : 0.0;
    }
    std::cout << "target mixture: sphere " << whole / (4.0 * pi) << ", narrow cone " << narrow / (2.0 * pi * widths[0])
              << ", untargeted octant pair " << outside / pi << " of their solid angles\n";
    assert(std::abs(whole / (4.0 * pi) - 1.0) < 0.01 && std::abs(narrow / (2.0 * pi * widths[0]) - 1.0) < 0.01);
    assert(std::abs(outside / pi - 1.0) < 0.02);
    std::cout << "progressive photon density estimator passed\n";
    return 0;
}
