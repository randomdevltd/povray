#!/usr/bin/env python3
"""Independent bulb-volume shadow profiles and fixed, predeclared quality metrics.

All blockers span the source and receiver's z range, so visibility depends
only on x/y. Integrating the sphere's z chord analytically leaves a 2-D
Cartesian quadrature, with no point-source or uniform-disk approximation.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

FLUX = 806
RADIUS = 0.03
HEIGHT = 1.8
FRAME_X = 0.025
FRAME_Z = 0.003125
LIMITS = {"profile_rmse": 0.025, "feature_contrast_error": 0.04,
          "width_relative_error": 0.10, "width_pixels": 1.5,
          "desired_pixel_rms": 0.02, "reference_convergence": 0.005}


def read_hdr(path):
    with path.open("rb") as stream:
        first = stream.readline().strip()
        if not first.startswith(b"#?"):
            raise ValueError("Expected Radiance RGBE HDR")
        header = []
        while line := stream.readline():
            if not line.strip():
                break
            header.append(line.strip())
        if b"FORMAT=32-bit_rle_rgbe" not in header:
            raise ValueError("Expected RGBE encoding")
        orientation = stream.readline().split()
        if orientation[0] != b"-Y" or orientation[2] != b"+X":
            raise ValueError("Expected top-down, left-to-right HDR")
        height, width = int(orientation[1]), int(orientation[3])
        pixels, quanta = [], []
        for _ in range(height):
            prefix = stream.read(4)
            if prefix[:2] == b"\x02\x02" and int.from_bytes(prefix[2:], "big") == width:
                channels = []
                for _ in range(4):
                    channel = bytearray()
                    while len(channel) < width:
                        code = stream.read(1)[0]
                        if code > 128:
                            channel.extend(stream.read(1) * (code - 128))
                        elif code:
                            channel.extend(stream.read(code))
                        else:
                            raise ValueError("Invalid HDR run")
                    if len(channel) != width:
                        raise ValueError("Invalid HDR scanline length")
                    channels.append(channel)
                encoded = list(zip(*channels))
            else:
                raw = prefix + stream.read(width * 4 - 4)
                encoded = [raw[i:i + 4] for i in range(0, len(raw), 4)]
            for red, green, blue, exponent in encoded:
                quantum = math.ldexp(1, exponent - 136) if exponent else 0
                pixels.append((red + green + blue) * quantum / 3)
                quanta.append(quantum)
    return width, height, pixels, quanta


def rod_blocks(rx, sx, sy, center, radius):
    dx = sx - rx
    denominator = dx * dx + sy * sy
    along = ((center - rx) * dx + 0.06 * sy) / denominator
    numerator = dx * 0.06 - sy * (center - rx)
    return 0 < along < 1 and numerator * numerator < radius * radius * denominator


def visible(case, rx, sx, sy):
    if case in (1, 3):
        xa = rx + (sx - rx) * 0.05995 / sy
        xb = rx + (sx - rx) * 0.06005 / sy
        if case == 1:
            return min(xa, xb) > 0
        return min(xa, xb) > -0.0005 and max(xa, xb) < 0.0005
    if case == 2:
        return not (rod_blocks(rx, sx, sy, -0.004, 0.00045)
                    or rod_blocks(rx, sx, sy, 0.004, 0.0012))
    if case == 4:
        return not rod_blocks(rx, sx, sy, 0, 0.0002)
    return True


def clear_profile(width, shape=0):
    if shape:
        return oracle_profile(width, 0, 32, shape)
    values = []
    for col in range(width):
        x = FRAME_X * ((col + 0.5) / width - 0.5)
        a = HEIGHT * HEIGHT + x * x
        values.append(FLUX / (4 * math.pi * math.pi) * HEIGHT
                      / (a * math.sqrt(a + FRAME_Z * FRAME_Z / 4)))
    return values


def oracle_profile(width, case, divisions, shape=0):
    if case == 0 and not shape:
        return clear_profile(width)
    step = 2 * RADIUS / divisions
    source = []
    for i in range(divisions):
        x = -RADIUS + (i + 0.5) * step
        for k in range(divisions):
            y = -RADIUS + (k + 0.5) * step
            radius_squared = RADIUS * RADIUS - x * x - y * y
            if shape or radius_squared > 0:
                source.append((x, y + HEIGHT, RADIUS if shape else math.sqrt(radius_squared)))
    volume = 8 * RADIUS ** 3 if shape else 4 * math.pi * RADIUS ** 3 / 3
    j = FLUX / (4 * math.pi * volume)
    weight = j * step * step / math.pi
    values = []
    for col in range(width):
        x = FRAME_X * ((col + 0.5) / width - 0.5)
        total = 0
        for sx, sy, half_z in source:
            if visible(case, x, sx, sy):
                a = (sx - x) ** 2 + sy * sy
                denominator = (math.sqrt(a + (half_z + FRAME_Z / 2) ** 2)
                               + math.sqrt(a + (half_z - FRAME_Z / 2) ** 2))
                total += 4 * sy * half_z / (a * denominator)
        values.append(total * weight)
    return values


def isotonic(values):
    blocks = []
    for value in values:
        blocks.append([value, 1])
        while len(blocks) >= 2 and blocks[-2][0] > blocks[-1][0]:
            right, left = blocks.pop(), blocks.pop()
            count = left[1] + right[1]
            blocks.append([(left[0] * left[1] + right[0] * right[1]) / count, count])
    return [value for value, count in blocks for _ in range(count)]


def crossing(profile, level):
    for i in range(1, len(profile)):
        if profile[i - 1] <= level <= profile[i] and profile[i] != profile[i - 1]:
            fraction = (level - profile[i - 1]) / (profile[i] - profile[i - 1])
            return FRAME_X * ((i - 0.5 + fraction) / len(profile) - 0.5)
    return None


def expected_profile(width, case, divisions, shape, cache=None):
    key = {"width": width, "case": case, "divisions": divisions, "shape": shape,
           "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    if cache and cache.exists():
        saved = json.loads(cache.read_text())
        if saved["key"] != key:
            raise ValueError("Reference cache geometry or implementation differs")
        return saved["clear"], saved["target"], saved["convergence"]
    clear = clear_profile(width, shape)
    target = oracle_profile(width, case, divisions, shape)
    coarse = oracle_profile(width, case, divisions // 2, shape)
    convergence = max(abs(a - b) / c for a, b, c in zip(target, coarse, clear))
    if convergence > LIMITS["reference_convergence"]:
        raise ValueError(f"Reference quadrature has not converged: {convergence}")
    if cache:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"key": key, "clear": clear, "target": target,
                                     "convergence": convergence}, indent=2))
    return clear, target, convergence


def analyze(path, case, divisions, reference=None, shape=0, rotation=0, cache=None):
    width, height, pixels, quanta = read_hdr(path)
    clear, target, convergence = expected_profile(width, case, divisions, shape, cache)
    measured = [sum(pixels[col::width]) / height for col in range(width)]
    normalized = [a / b for a, b in zip(measured, clear)]
    expected = [a / b for a, b in zip(target, clear)]
    profile_rmse = math.sqrt(sum((a - b) ** 2 for a, b in zip(normalized, expected)) / width)
    pixel_rms = math.sqrt(sum((value / clear[i % width] - expected[i % width]) ** 2
                              for i, value in enumerate(pixels)) / len(pixels))
    index_low, index_high = min(range(width), key=target.__getitem__), max(range(width), key=target.__getitem__)
    def band(values, center):
        indices = range(max(0, center - 1), min(width, center + 2))
        indices = list(indices)
        return sum(values[i] for i in indices) / len(indices)
    contrast = band(normalized, index_high) - band(normalized, index_low)
    expected_contrast = band(expected, index_high) - band(expected, index_low)
    result = {"image": str(path), "case": case, "shape": shape, "rotation_degrees": rotation,
              "width": width, "height": height,
              "flux_lumens": FLUX, "reference_divisions": divisions,
              "reference_max_convergence_error": convergence,
              "profile_rmse": profile_rmse, "pixel_rms": pixel_rms,
              "contrast": contrast, "expected_contrast": expected_contrast,
              "contrast_error": abs(contrast - expected_contrast),
              "mean_relative_hdr_quantum": sum(q / clear[i % width] for i, q in enumerate(quanta)) / len(quanta),
              "limits": LIMITS, "geometry_pass": profile_rmse <= LIMITS["profile_rmse"]
              and abs(contrast - expected_contrast) <= LIMITS["feature_contrast_error"],
              "noise_target_met": pixel_rms <= LIMITS["desired_pixel_rms"]}
    for label, condition in [("lit", lambda value: value > 0.99),
                             ("edge", lambda value: 0.05 < value < 0.95),
                             ("dark", lambda value: value < 0.01)]:
        residuals = [(value / clear[i % width] - expected[i % width]) ** 2
                     for i, value in enumerate(pixels) if condition(expected[i % width])]
        result[label + "_pixel_rms"] = math.sqrt(sum(residuals) / len(residuals)) if residuals else None
    if case == 1:
        fit = isotonic(normalized)
        low, high = crossing(fit, 0.1), crossing(fit, 0.9)
        target_low, target_high = crossing(expected, 0.1), crossing(expected, 0.9)
        measured_width = high - low if low is not None and high is not None else None
        expected_width = target_high - target_low
        tolerance = max(LIMITS["width_pixels"] * FRAME_X / width,
                        LIMITS["width_relative_error"] * expected_width)
        result.update({"penumbra_10_90_m": measured_width, "expected_penumbra_10_90_m": expected_width,
                       "penumbra_tolerance_m": tolerance})
        result["geometry_pass"] &= measured_width is not None and abs(measured_width - expected_width) <= tolerance
    if reference:
        rw, rh, ref_pixels, _ = read_hdr(reference)
        if (rw, rh) != (width, height):
            raise ValueError("Image reference framing/resolution differs")
        result["rms_against_rendered_reference"] = math.sqrt(sum(((a - b) / clear[i % width]) ** 2
                                                               for i, (a, b) in enumerate(zip(pixels, ref_pixels))) / len(pixels))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--case", type=int, choices=range(5), default=1)
    parser.add_argument("--divisions", type=int, default=512)
    parser.add_argument("--shape", type=int, choices=[0, 1], default=0)
    parser.add_argument("--rotation", type=float, default=0)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--oracle-cache", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = analyze(args.image, args.case, args.divisions, args.reference, args.shape, args.rotation, args.oracle_cache)
    print(json.dumps(result, indent=2))
    if args.check and not result["geometry_pass"]:
        raise SystemExit(1)
