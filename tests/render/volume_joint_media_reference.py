#!/usr/bin/env python3
"""Cartesian finite-source radiative transfer through piecewise constant voxels."""
import argparse
import json
import math
from pathlib import Path

ABSORPTION = (0.2, 0.4, 0.6)
SCATTERING = (0.04, 0.03, 0.02)
COLOR = (1, 0.5, 0.2)


def regions(case):
    edges = [0, 0.5, 1] if case in (1, 4) else [0, 0.25, 0.75, 1] if case == 2 else [0, 1]
    return [(a, b, (0.5 if a < 0.5 else 1.5) if case in (1, 4)
             else 0.5 if case == 2 and a == 0.25 else 1) for a, b in zip(edges, edges[1:])]


def segment_box_fraction(origin, target, lower, upper):
    enter, leave = 0.0, 1.0
    for a, b, lo, hi in zip(origin, target, lower, upper):
        delta = b - a
        if delta == 0:
            if not lo <= a <= hi:
                return 0.0
        else:
            first, last = sorted(((lo - a) / delta, (hi - a) / delta))
            enter, leave = max(enter, first), min(leave, last)
    return max(0.0, leave - enter)


def column_density(origin, target, case):
    length = math.dist(origin, target)
    return length * sum(density * segment_box_fraction(origin, target,
        (-0.1, -0.1, a), (0.1, 0.1, b)) for a, b, density in regions(case))


def phase(cosine, eccentricity):
    return (1 - eccentricity ** 2) / (4 * math.pi *
        (1 + eccentricity ** 2 - 2 * eccentricity * cosine) ** 1.5)


def reference(x=0, y=0, case=0, eccentricity=0, source_divisions=12, depth_divisions=64):
    source = []
    for i in range(source_divisions):
        sx = 1 + 0.2 * (i + 0.5) / source_divisions
        for j in range(source_divisions):
            sy = -0.1 + 0.2 * (j + 0.5) / source_divisions
            for k in range(source_divisions):
                sz = 0.35 + 0.3 * (k + 0.5) / source_divisions
                source.append((sx, sy, sz))
    luminance = sum(a * b for a, b in zip(COLOR, (0.297, 0.589, 0.114)))
    emitted_weight = 100 / (4 * math.pi * luminance * len(source))
    result = [0.0] * 3
    for low, high, density in regions(case):
        step = (high - low) / depth_divisions
        for index in range(depth_divisions):
            point = (x, y, low + (index + 0.5) * step)
            camera_column = column_density((x, y, 0), point, case)
            for target in source:
                if case == 3 and segment_box_fraction(point, target, (0.49, -1, -1), (0.51, 1, 0.5)) > 0:
                    continue
                distance_squared = sum((a - b) ** 2 for a, b in zip(target, point))
                distance = math.sqrt(distance_squared)
                cosine = (target[2] - point[2]) / distance
                weight = step * density * emitted_weight * phase(cosine, eccentricity) / distance_squared
                optical_column = camera_column + column_density(point, target, case)
                for channel in range(3):
                    extinction = ABSORPTION[channel] + SCATTERING[channel]
                    result[channel] += weight * SCATTERING[channel] * COLOR[channel] * math.exp(-extinction * optical_column)
    return result


def self_check():
    for g in [0, -0.4, 0.4]:
        integral = sum(phase(-1 + (i + 0.5) * 2 / 20000, g) for i in range(20000)) * 4 * math.pi / 20000
        assert abs(integral - 1) < 1e-7
    assert abs(column_density((0, 0, 0), (0, 0, 1), 0) - 1) < 1e-12
    assert abs(column_density((0, 0, 0), (0, 0, 1), 1) - 1) < 1e-12
    assert abs(column_density((0, 0, 0), (0, 0, 1), 2) - 0.75) < 1e-12
    assert abs(segment_box_fraction((0, 0, 0), (2, 0, 0), (0.5, -1, -1), (1, 1, 1)) - 0.25) < 1e-12


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=int, choices=range(5), default=0)
    parser.add_argument('--phase', type=float, default=0)
    parser.add_argument('--source-divisions', type=int, default=12)
    parser.add_argument('--depth-divisions', type=int, default=64)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--exact-control', type=Path)
    args = parser.parse_args()
    self_check()
    fine = reference(case=args.case, eccentricity=args.phase, source_divisions=args.source_divisions, depth_divisions=args.depth_divisions)
    coarse = reference(case=args.case, eccentricity=args.phase, source_divisions=args.source_divisions // 2, depth_divisions=args.depth_divisions // 2)
    convergence = [abs(a - b) / a for a, b in zip(fine, coarse)]
    if max(convergence) > 0.005:
        raise ValueError(f'Independent source/depth integral not converged: {convergence}')
    result = {'case': args.case, 'phase': args.phase, 'rgb': fine, 'coarse_rgb': coarse,
              'relative_convergence': convergence, 'source_divisions': args.source_divisions,
              'depth_divisions_per_region': args.depth_divisions}
    if args.image:
        from volume_transport_reference import read_ppm
        width, height, measured = read_ppm(args.image)
        if args.exact_control:
            cw, ch, control = read_ppm(args.exact_control)
            if (cw, ch, control) != (width, height, measured):
                raise AssertionError('Collector evaluate-all differs from fixed-quadrature control')
            result['exact_control_pass'] = True
        targets = []
        for row in range(height):
            y = 0.02 * (0.5 - (row + 0.5) / height)
            for col in range(width):
                x = 0.02 * ((col + 0.5) / width - 0.5)
                targets.extend(reference(x, y, args.case, args.phase,
                    args.source_divisions, args.depth_divisions))
        result['channels'] = []
        for channel in range(3):
            got, expected = measured[channel::3], targets[channel::3]
            mean, target_mean = sum(got) / len(got), sum(expected) / len(expected)
            limit = max(2 / 65535, 0.03 * target_mean)
            rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(got, expected)) / len(got))
            result['channels'].append({'mean': mean, 'reference_mean': target_mean,
                'mean_error_limit': limit, 'mean_pass': abs(mean-target_mean) <= limit,
                'relative_pixel_rms': rms / target_mean})
        result['mean_pass'] = all(c['mean_pass'] for c in result['channels'])
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text)
    if args.image and not result['mean_pass']:
        raise SystemExit(1)
