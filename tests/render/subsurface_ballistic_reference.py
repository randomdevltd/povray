#!/usr/bin/env python3
"""Compare an unlit slab with independent unpolarised Fresnel and Beer extinction."""
import argparse
import math
from pathlib import Path

from volume_transport_reference import read_ppm


def transmission(cosine, n_from, n_to):
    sine2 = (n_from / n_to) ** 2 * (1 - cosine ** 2)
    if sine2 >= 1:
        return 0.0, 0.0
    transmitted = math.sqrt(1 - sine2)
    rs = (n_from * cosine - n_to * transmitted) / (n_from * cosine + n_to * transmitted)
    rp = (n_to * cosine - n_from * transmitted) / (n_to * cosine + n_from * transmitted)
    return 1 - (rs * rs + rp * rp) / 2, transmitted


def check(folder):
    for angle in (0, 45, 70):
        cosine = math.cos(math.radians(angle))
        entry, inside = transmission(cosine, 1.0, 1.44)
        exit_, outside = transmission(inside, 1.44, 1.0)
        assert abs(outside - cosine) < 1e-12
        for kind in ('clear', 'finite', 'equal', 'legacy', 'conserve'):
            # At reflectance 1 both inversion tables end at alpha'=1, hence sigma_t'=1/Length.
            length = 2 if kind == 'finite' else 1e9
            path = 1 / (cosine if kind == 'equal' else inside)
            expected = math.exp(-path / length)
            if kind not in ('equal', 'legacy'):
                expected *= entry * exit_
            width, height, pixels = read_ppm(folder / f'ssball_{kind}{angle}.ppm')
            assert (width, height) == (8, 8)
            error = max(abs(value - expected) for value in pixels)
            assert error < 8 / 65535, (kind, angle, expected, min(pixels), max(pixels), error)
    for name, expected in (('opaque', 0.0),
                           ('wedge', transmission(1.0, 1.0, 1.44)[0] *
                            transmission(math.cos(math.radians(30)), 1.44, 1.0)[0] * math.exp(-1e-9))):
        _, _, pixels = read_ppm(folder / f'ssball_{name}.ppm')
        assert max(abs(value - expected) for value in pixels) < 8 / 65535, (name, expected, pixels)
    print('subsurface_ballistic: Fresnel interfaces, finite extinction, equal indices and method 1 agree')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    check(parser.parse_args().folder)
