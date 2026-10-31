#!/usr/bin/env python3
"""Check SSLT adapters against equivalent point intensity and photon flux density."""
import argparse
from pathlib import Path

from volume_transport_reference import compare, read_ppm


def check(folder):
    def pixels(name):
        width, height, values = read_ppm(folder / f"sslt_volume_{name}.ppm")
        assert (width, height) == (12, 12), (width, height)
        assert min(values) >= 0 and 0.001 < sum(values) / len(values) < 0.9, name
        return values

    # J = Phi/(4*pi); the classic inverse-square colour is J/pi.
    compare("SSLT far physical intensity", pixels("far_volume"), pixels("far_point"), 0.03)
    compare("SSLT near physical photon irradiance", pixels("near_photons"), pixels("near_direct"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    check(parser.parse_args().folder)
