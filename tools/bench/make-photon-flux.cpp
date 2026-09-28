#include "core/lighting/photons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pov;

static void balance(std::vector<Photon>& photons, int first, int last)
{
    if (first > last)
        return;
    Vector3d lo(photons[first].Loc), hi(lo);
    for (int i = first + 1; i <= last; ++i)
        for (int a = 0; a < 3; ++a)
        {
            lo[a] = std::min(lo[a], double(photons[i].Loc[a]));
            hi[a] = std::max(hi[a], double(photons[i].Loc[a]));
        }
    int axis = 0;
    for (int a = 1; a < 3; ++a)
        if (hi[a] - lo[a] > hi[axis] - lo[axis])
            axis = a;
    int mid = (first + last) / 2;
    std::nth_element(photons.begin() + first, photons.begin() + mid, photons.begin() + last + 1,
                     [axis](const Photon& a, const Photon& b) { return a.Loc[axis] < b.Loc[axis]; });
    photons[mid].info = axis;
    balance(photons, first, mid - 1);
    balance(photons, mid + 1, last);
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 7)
    {
        std::fprintf(stderr, "usage: make-photon-flux output [step [irradiance [angle-degrees [height [patch-radius]]]]]\n");
        return 2;
    }
    double step = argc > 2 ? std::atof(argv[2]) : 0.05;
    double power = argc > 3 ? std::atof(argv[3]) : 1.0;
    double angle = argc > 4 ? std::atof(argv[4]) : 0.0;
    double height = argc > 5 ? std::atof(argv[5]) : 0.0;
    double patch = argc > 6 ? std::atof(argv[6]) : 20.0;
    if (!std::isfinite(step) || !std::isfinite(power) || !std::isfinite(angle) || !std::isfinite(height) ||
        !std::isfinite(patch) || step < 0.01 || step > 16.0 || power < 0.0 || angle <= -90.0 || angle > 180.0 || patch < 0.0)
        return 2;
    std::vector<Photon> photons;
    int count = int(16.0 / step);
    for (int x = 0; x < count; ++x)
        for (int z = 0; z < count; ++z)
        {
            double px = -8.0 + (x + 0.5) * step, pz = -8.0 + (z + 0.5) * step;
            if (px * px + pz * pz > patch * patch)
                continue;
            Photon photon{};
            photon.Loc = PhotonVector3d(px, height, pz);
            photon.colour = PhotonColour(RGBColour(power * step * step));
            photon.theta = static_cast<signed char>((0.5 * M_PI - angle * M_PI / 180.0) * 127.0 / M_PI);
            photon.phi = 0;
            photons.push_back(photon);
        }
    balance(photons, 0, int(photons.size()) - 1);
    FILE* out = std::fopen(argv[1], "wb");
    if (!out)
        return 1;
    int surface = int(photons.size()), media = 0;
    bool ok = std::fwrite(&surface, sizeof(surface), 1, out) == 1 &&
              std::fwrite(photons.data(), sizeof(Photon), photons.size(), out) == photons.size() &&
              std::fwrite(&media, sizeof(media), 1, out) == 1;
    ok = std::fclose(out) == 0 && ok;
    std::printf("%d deposits, %.9g irradiance, %zu bytes per deposit\n", surface, power, sizeof(Photon));
    return ok ? 0 : 1;
}
