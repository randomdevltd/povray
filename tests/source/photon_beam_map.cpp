#include "core/lighting/progressivevolumemap.h"

#include <cassert>
#include <random>
#include <iostream>

int main()
{
    pov::ProgressiveVolumePhotonMap map;
    auto domain = std::make_shared<pov::ProgressiveVolumeDomain>();
    auto otherDomain = std::make_shared<pov::ProgressiveVolumeDomain>();
    otherDomain->media.push_back(nullptr);
    std::mt19937_64 random(19);
    std::uniform_real_distribution<double> coordinate(-3, 3);
    for (int i = 0; i < 2048; ++i)
    {
        pov::ProgressiveVolumePhoton photon;
        photon.point = pov::Vector3d(coordinate(random), coordinate(random), coordinate(random));
        photon.direction = pov::Vector3d(coordinate(random), coordinate(random), coordinate(random)).normalized();
        photon.length = std::abs(coordinate(random));
        photon.flux = pov::MathColour(1);
        photon.domain = (i % 2) ? domain : otherDomain;
        map.photons.push_back(photon);
    }
    map.Build();
    for (int i = 0; i < 100; ++i)
    {
        const pov::Vector3d point(coordinate(random), coordinate(random), coordinate(random));
        const double radius = 0.2 + std::abs(coordinate(random)) / 4;
        int expected = 0, found = 0, expectedOwned = 0, owned = 0;
        double sum = 0, indexed = 0;
        for (const auto& photon : map.photons)
        {
            expected += photon.DistanceSquared(point) <= radius * radius;
            expectedOwned += *photon.domain == *domain && photon.DistanceSquared(point) <= radius * radius;
            sum += photon.Integral(point, radius)[0];
        }
        map.Visit(point, radius, [&](const pov::ProgressiveVolumePhoton& photon) {
            ++found;
            owned += *photon.domain == *domain;
            indexed += photon.Integral(point, radius)[0];
        });
        assert(found == expected);
        assert(owned == expectedOwned);
        assert(std::abs(sum - indexed) < 1e-10);
        std::vector<double> distances;
        for (const auto& photon : map.photons)
            if (*photon.domain == *domain)
                distances.push_back(photon.DistanceSquared(point));
        std::sort(distances.begin(), distances.end());
        const double support = std::min(std::sqrt(distances[31]), 4 * map.initialRadius);
        assert(std::abs(support - map.RadiusAt(point, *domain)) < 1e-10);
    }
    auto other = *domain;
    other.media.push_back(nullptr);
    assert(!(*domain == other));
    other = *domain;
    other.modifiers.push_back(nullptr);
    other.below.push_back(1);
    assert(!(*domain == other));
    std::cout << "photon beam map: indexed segment gathers, nearest distances and domain identity passed\n";
}
