#!/usr/bin/env python3
"""Compare rendered radiance with independent Cartesian volume quadrature.

The input coefficient j is emission per volume per solid angle. A white
Lambertian receiver reads integral(j * visibility * T * cos(theta) / r^2)/pi.
The reference neither samples chords nor uses a source bounding sphere.
"""

import argparse
import math
from pathlib import Path


def read_ppm(path):
    data = path.read_bytes()
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[pos:end])
        pos = end
    assert tokens[0] == b"P6" and int(tokens[3]) == 65535, path
    width, height = map(int, tokens[1:3])
    raw = data[pos + 1:]
    assert len(raw) == width * height * 6, path
    return width, height, [int.from_bytes(raw[i:i + 2], "big") / 65535
                           for i in range(0, len(raw), 2)]


def top_down_x(column, width, frame):
    """The supplied up=z/deferred look_at camera resolves to a negative right basis."""
    return frame * (0.5 - (column + 0.5) / width)


def boxes(case):
    boxes = [(-0.5, 0.5, 1, 2, -0.4, 0.4)]
    if case in (2, 3):
        boxes = [(-0.73, -0.22, 1.2, 1.8, -0.3, 0.3),
                 (0.31, 0.81, 1.2, 1.8, -0.3, 0.3)]
    if case == 5:
        boxes = [(-0.5, 0.5, 0, 1, -0.5, 0.5)]
    return boxes


def pigment(case, x):
    if case == 4:
        return (1, 0, 0)
    if case in (2, 3):
        t = (x + 1) / 2
        return (1 - 0.95 * t, 0.2 + 0.2 * t, 0.05 + 0.95 * t)
    return (1, 1, 1)


def coefficient(case, flux=10):
    integral = 0
    for x0, x1, y0, y1, z0, z1 in boxes(case):
        color = pigment(case, (x0 + x1) / 2)
        integral += (x1 - x0) * (y1 - y0) * (z1 - z0) * sum(
            a * b for a, b in zip(color, (0.297, 0.589, 0.114)))
    return flux / (4 * math.pi * integral)


def cells(case, divisions):
    for x0, x1, y0, y1, z0, z1 in boxes(case):
        dx, dy, dz = (x1 - x0) / divisions, (y1 - y0) / divisions, (z1 - z0) / divisions
        for a in range(divisions):
            x = x0 + (a + 0.5) * dx
            color = pigment(case, x)
            for b in range(divisions):
                y = y0 + (b + 0.5) * dy
                for c in range(divisions):
                    yield x, y, z0 + (c + 0.5) * dz, color, dx * dy * dz


def reference(width, height, case=1, sigma=0, elevation=0, gain=1, divisions=20,
              frame=2, probe=0, global_sigma=0, camera_distance=0.25):
    source = list(cells(case, divisions))
    result = []
    for row in range(height):
        rz = frame * (0.5 - (row + 0.5) / height)
        for col in range(width):
            rx = top_down_x(col, width, frame)
            rgb = [0.0, 0.0, 0.0]
            for sx, sy0, sz, color, dv in source:
                sy = sy0 + elevation - probe
                if sy <= 0:
                    continue
                dx, dz = sx - rx, sz - rz
                distance = math.sqrt(dx * dx + sy * sy + dz * dz)
                if case == 3:
                    xa = rx + dx * 0.49 / sy
                    xb = rx + dx * 0.51 / sy
                    if min(xa, xb) <= 0.13 and max(xa, xb) >= -0.13:
                        continue
                transmittance = 1.0
                if sigma:
                    fractions = [(sy0 - 1) / sy]
                    if dx:
                        fractions.append(((sx + 0.5) if dx > 0 else (0.5 - sx)) / abs(dx))
                    if dz:
                        fractions.append(((sz + 0.4) if dz > 0 else (0.4 - sz)) / abs(dz))
                    transmittance = math.exp(-sigma * min(fractions) * distance)
                transmittance *= math.exp(-global_sigma * (distance + camera_distance))
                weight = gain * transmittance * sy * dv / (math.pi * distance ** 3)
                for channel in range(3):
                    rgb[channel] += color[channel] * weight
            result.extend(rgb)
    return result


def compare(label, got, expected, relative=0.08):
    for channel in range(3):
        actual = got[channel::3]
        target = expected[channel::3]
        scale = sum(target) / len(target)
        error = sum(abs(a - b) for a, b in zip(actual, target)) / len(target)
        limit = max(2 / 65535, relative * scale)
        if error > limit:
            raise AssertionError(f"{label} channel {channel}: MAE {error:.7g}, limit {limit:.7g}, reference mean {scale:.7g}")
        mean_error = abs(sum(actual) / len(actual) - scale)
        if mean_error > max(2 / 65535, relative * 0.4 * scale):
            raise AssertionError(f"{label} channel {channel}: mean error {mean_error:.7g}, reference mean {scale:.7g}")
        print(f"{label} channel {channel}: mean {sum(actual)/len(actual):.7g}, reference {scale:.7g}, MAE {error:.3g}")


def rectangle_potential(x, z, depth):
    radius = math.sqrt(x * x + z * z + depth * depth)
    return (x * math.asinh(z / math.hypot(x, depth))
            + z * math.asinh(x / math.hypot(z, depth))
            - depth * math.atan2(x * z, depth * radius))


def cube_reference(width, height, gap=0, probe=0):
    """Integrate y/r^3 in y analytically, then integrate the rectangular 1/r potential."""
    values = []
    for row in range(height):
        z = 0.02 * (0.5 - (row + 0.5) / height)
        for col in range(width):
            x = top_down_x(col, width, 0.02)
            planes = []
            for depth in [max(0, gap - probe), 1 + gap - probe]:
                value = 0
                for xx, sign_x in [(-0.5 - x, -1), (0.5 - x, 1)]:
                    for zz, sign_z in [(-0.5 - z, -1), (0.5 - z, 1)]:
                        value += sign_x * sign_z * rectangle_potential(xx, zz, depth)
                planes.append(value)
            values.extend([0.15 * (planes[0] - planes[1]) / math.pi] * 3)
    return values


def half_box_reference(width, height):
    values = []
    for row in range(height):
        z = 2 * (0.5 - (row + 0.5) / height)
        for col in range(width):
            x = top_down_x(col, width, 2)
            planes = []
            for depth in [1, 2]:
                value = 0
                for xx, sign_x in [(-0.5 - x, -1), (-x, 1)]:
                    for zz, sign_z in [(-0.5 - z, -1), (0.5 - z, 1)]:
                        value += sign_x * sign_z * rectangle_potential(xx, zz, depth)
                planes.append(value)
            values.extend([0.2 * (planes[0] - planes[1]) / math.pi] * 3)
    return values


def check(folder, subset="all"):
    if subset == "photons":
        width, height, pixels = read_ppm(folder / "photon_gathered.ppm")
        coarse = reference(width, height, divisions=20, gain=coefficient(1))
        fine = reference(width, height, divisions=32, gain=coefficient(1))
        compare("photon reference convergence", coarse, fine, 0.025)
        compare("physical source photon irradiance", pixels, fine)
        return
    w, h, white = read_ppm(folder / "white.ppm")
    white_j = coefficient(1)
    for name, options in [("white", {"gain": white_j}), ("red", {"case": 4, "gain": coefficient(4)}),
                          ("absorbed", {"sigma": 0.7, "gain": white_j}),
                          ("shaped", {"case": 2, "gain": coefficient(2)}),
                          ("shadow", {"case": 3, "gain": coefficient(3)}),
                          ("distant", {"elevation": 20, "gain": 100 * white_j}),
                          ("remote", {"elevation": 100000, "gain": 100000000 * white_j}),
                          ("fog", {"global_sigma": 0.7, "gain": white_j}),
                          ("atmosphere_box", {"global_sigma": 0.7, "gain": white_j})]:
        wi, hi, pixels = read_ppm(folder / f"{name}.ppm")
        assert (wi, hi) == (w, h)
        coarse = reference(w, h, divisions=16, **options)
        fine = reference(w, h, divisions=24, **options)
        compare(name + " reference convergence", coarse, fine, 0.025)
        compare(name, pixels, fine)
    for name in ["sparse", "scaled"]:
        _, _, pixels = read_ppm(folder / f"{name}.ppm")
        target = [value / 4 for value in white] if name == "scaled" else white
        compare(name + " transport scaling", pixels, target, 0.10 if name == "sparse" else 0.06)
    if subset == "lumens":
        return
    for gap in [0, 0.02, 0.1, 0.5, 1, 2, 4, 8, 16]:
        name = f"cube_{gap}"
        _, _, pixels = read_ppm(folder / f"{name}.ppm")
        target = cube_reference(w, h, gap=gap)
        compare(name + " mean-chord reference", pixels, target)
    _, _, center = read_ppm(folder / "cube_center.ppm")
    center_reference = cube_reference(w, h, probe=0.5)
    compare("cube center", center, center_reference)
    _, _, scaled = read_ppm(folder / "cube_scaled.ppm")
    _, _, adjacent = read_ppm(folder / "cube_0.ppm")
    compare("nits geometric scaling", scaled, adjacent, 0.06)
    _, _, half_box = read_ppm(folder / "half_box.ppm")
    _, _, clipped_box = read_ppm(folder / "clipped_box.ppm")
    target = half_box_reference(w, h)
    compare("half box mean-chord reference", half_box, target)
    compare("clipped box mean-chord reference", clipped_box, target)
    compare("clipped and explicit source equivalence", clipped_box, half_box, 0.06)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--subset", choices=["all", "lumens", "photons"], default="all")
    args = parser.parse_args()
    check(args.folder, args.subset)
