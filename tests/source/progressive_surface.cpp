#include "core/lighting/emitter.h"
#include "core/lighting/photonaim.h"
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

    std::mt19937_64 aiming(13);
    for (int i = 0; i < 10000; ++i)
    {
        const double x = unit(aiming), y = unit(aiming);
        double cx, cy, bx, by;
        const double r = pov::PhotonAimDomain::Disc(x, y, cx, cy);
        pov::PhotonAimDomain::Square(r, std::atan2(cy, cx), bx, by);
        assert(std::abs(bx - x) < 1e-9 && std::abs(by - y) < 1e-9);
    }
    // a sphere and a flat box seen from a point light and from a small area light, and their shadows under a parallel light
    const auto hits = [](const pov::Vector3d& o, const pov::Vector3d& d, const pov::PhotonAimBound& b) {
        if (b.sphereRadius > 0.0)
        {
            const pov::Vector3d m = b.sphereCentre - o;
            const double along = dot(m, d);
            return along > 0.0 && m.lengthSqr() - along * along <= b.sphereRadius * b.sphereRadius;
        }
        double t0 = 0.0, t1 = 1e30;
        for (int k = 0; k < 3; ++k)
        {
            if (std::abs(d[k]) < 1e-15)
            {
                if (o[k] < b.low[k] || o[k] > b.high[k])
                    return false;
                continue;
            }
            const double a = (b.low[k] - o[k]) / d[k], c = (b.high[k] - o[k]) / d[k];
            t0 = std::max(t0, std::min(a, c));
            t1 = std::min(t1, std::max(a, c));
        }
        return t0 <= t1;
    };
    std::vector<pov::PhotonAimBound> bounds(2);
    bounds[0].sphereCentre = pov::Vector3d(0, 0, 5);
    bounds[0].sphereRadius = 1.0;
    bounds[0].low = bounds[0].sphereCentre - pov::Vector3d(1.0);
    bounds[0].high = bounds[0].sphereCentre + pov::Vector3d(1.0);
    bounds[0].weight = 1.0;
    bounds[0].target = 0;
    bounds[1].low = pov::Vector3d(2, -3, 4);
    bounds[1].high = pov::Vector3d(6, -2.9, 9);
    bounds[1].weight = 3.0;
    bounds[1].target = 1;
    for (double lightRadius : {0.0, 0.3})
    {
        pov::PhotonAimDomain cap;
        cap.BuildCap(pov::Vector3d(0.0), lightRadius, bounds, nullptr, nullptr, 0.0);
        assert(!cap.Empty());
        const unsigned int samples = 1u << 20;
        double sphere = 0.0, box = 0.0, total = 0.0;
        for (double m : cap.mass)
            total += m;
        assert(std::abs(total - 1.0) < 1e-9);
        for (unsigned int i = 0; i < samples; ++i)
        {
            // every direction that reaches a bound from anywhere on the light lies in a cell of positive mass
            const double z = 1.0 - 2.0 * unit(aiming), r = std::sqrt(std::max(0.0, 1.0 - z * z)), turn = 2.0 * pi * unit(aiming);
            const pov::Vector3d d(r * std::cos(turn), r * std::sin(turn), z);
            const pov::Vector3d o = pov::Vector3d(unit(aiming) - 0.5, unit(aiming) - 0.5, 0.0) * (lightRadius * 1.4);
            if (hits(o, d, bounds[0]) || hits(o, d, bounds[1]))
            {
                std::uint32_t cell;
                assert(cap.Locate(d, cell) && cap.mass[cell] > 0.0);
            }
            const auto pick = cap.Sample(unit(aiming), unit(aiming));
            box += pick.target == 1 ? 1.0 / samples : 0.0;
            if (lightRadius == 0.0 && hits(pov::Vector3d(0.0), cap.Direction(pick.x, pick.y), bounds[0]))
                sphere += 1.0 / (samples * cap.Pdf(pick.cell));
        }
        const double exact = 2.0 * pi * (1.0 - std::sqrt(1.0 - 1.0 / 25.0));
        std::cout << "aiming cap, light radius " << lightRadius << ": sphere solid angle " << sphere / exact
                  << " of exact, weight 3 box share " << box << "\n";
        assert(lightRadius > 0.0 || std::abs(sphere / exact - 1.0) < 0.01);
        assert(box > pov::PhotonAimDomain::targetShare * 0.75);
    }
    pov::PhotonAimDomain sheet;
    sheet.BuildPlane(pov::Vector3d(0, 0, -20), pov::Vector3d(0, 0, 1), bounds, nullptr, 0.0);
    double area = 0.0;
    const unsigned int samples = 1u << 20;
    for (unsigned int i = 0; i < samples; ++i)
    {
        const auto pick = sheet.Sample(unit(aiming), unit(aiming));
        const pov::Vector3d p = sheet.Point(pick.x, pick.y);
        if ((pov::Vector3d(p[pov::X], p[pov::Y], 5.0) - bounds[0].sphereCentre).lengthSqr() <= 1.0)
            area += 1.0 / (samples * sheet.Pdf(pick.cell));
    }
    std::cout << "aiming plane: sphere shadow " << area / pi << " of exact\n";
    assert(std::abs(area / pi - 1.0) < 0.01);
    std::cout << "progressive photon density estimator passed\n";
    return 0;
}
