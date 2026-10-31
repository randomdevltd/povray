#!/usr/bin/env python3
"""An isotropic single-scattering oracle from the sphere's analytic volume integral.

For isotropic emissivity j, integral(Li dOmega)=j*integral_V(1/r^2 dV).
The receiving source term is sigma_s/(4*pi) times that angular integral.
No surface cosine or Lambertian division enters this response.
"""

import argparse
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from volume_transport_reference import compare, read_ppm, top_down_x


def sphere_integral(distance, radius=1):
    if distance < radius * 1e-7:
        return 4 * math.pi * radius
    if abs(distance - radius) < radius * 1e-12:
        return 2 * math.pi * radius
    return (2 * math.pi * radius + math.pi * (radius * radius - distance * distance)
            / distance * math.log((radius + distance) / abs(radius - distance)))


def scattering_reference(width, height, receiver):
    values = []
    steps = 128
    sigma = 0.01
    step = 0.04 / steps
    j = 100 / (4 / 3)
    for row in range(height):
        y = 0.002 * (0.5 - (row + 0.5) / height)
        for col in range(width):
            x = receiver + 0.002 * ((col + 0.5) / width - 0.5)
            total = 0
            for index in range(steps):
                z = -0.02 + (index + 0.5) * step
                distance = math.sqrt(x * x + y * y + z * z)
                source = sigma * j * sphere_integral(distance) / (4 * math.pi)
                total += source * math.exp(-sigma * (z + 0.02)) * step
            values.extend([total] * 3)
    return values


def surface_reference(width, height):
    values = []
    for row in range(height):
        z = 0.2 * (0.5 - (row + 0.5) / height)
        for col in range(width):
            x = top_down_x(col, width, 0.2)
            values.extend([2 / (x * x + z * z + 4) ** 1.5] * 3)
    return values


def hemisphere_reference(width, height, divisions):
    cells = []
    dx = 1 / divisions
    dy = dz = 2 / divisions
    for i in range(divisions):
        x = -1 + (i + 0.5) * dx
        for j in range(divisions):
            y = -1 + (j + 0.5) * dy
            for k in range(divisions):
                z = -1 + (k + 0.5) * dz
                if x * x + y * y + z * z < 1:
                    cells.append((x, y + 2, z))
    volume = 2 * math.pi / 3
    area = 3 * math.pi
    weight = area / (4 * volume * math.pi) * dx * dy * dz
    values = []
    for row in range(height):
        z = 0.2 * (0.5 - (row + 0.5) / height)
        for col in range(width):
            x = top_down_x(col, width, 0.2)
            value = sum(sy / ((sx - x) ** 2 + sy * sy + (sz - z) ** 2) ** 1.5
                        for sx, sy, sz in cells) * weight
            values.extend([value] * 3)
    return values


def check(folder):
    assert abs(0.75 * sphere_integral(0) / (4 * math.pi) - 0.75) < 1e-12
    assert abs(0.75 * sphere_integral(1) / (4 * math.pi) - 0.375) < 1e-12
    for name, receiver in [("center", 0), ("outside", 1.002), ("farther", 2), ("scaled", 0)]:
        width, height, pixels = read_ppm(folder / f"{name}.ppm")
        target = scattering_reference(width, height, receiver)
        compare(name + " isotropic single scattering", pixels, target)
    for name in ["surface", "tiny_native", "tiny_transformed"]:
        width, height, pixels = read_ppm(folder / f"{name}.ppm")
        compare(name + " mean-chord sphere", pixels, surface_reference(width, height))
    width, height, pixels = read_ppm(folder / "hemisphere.ppm")
    coarse = hemisphere_reference(width, height, 24)
    fine = hemisphere_reference(width, height, 40)
    compare("hemisphere Cartesian reference convergence", coarse, fine, 0.02)
    compare("hemisphere including planar cap", pixels, fine)
    print("volume_scattering: omitted source-connection extinction is bounded by 0.041%")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    check(parser.parse_args().folder)
