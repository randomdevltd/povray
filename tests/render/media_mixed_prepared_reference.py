#!/usr/bin/env python3
"""Cartesian source and camera integration through two overlapping optical boxes."""
import argparse
import json
import math
from pathlib import Path
import volume_joint_media_reference as integral
from volume_transport_reference import read_ppm


def regions(case):
    return [(0, 0.25, 1), (0.25, 0.75, 1.5), (0.75, 1, 1)]


def column(origin, target, case):
    return math.dist(origin, target) * (
        integral.segment_box_fraction(origin, target, (-0.1, -0.1, 0), (0.1, 0.1, 1))
        + 0.5 * integral.segment_box_fraction(origin, target, (-0.09, -0.09, 0.25), (0.09, 0.09, 0.75)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    integral.regions, integral.column_density = regions, column
    fine = integral.reference()
    coarse = integral.reference(source_divisions=6, depth_divisions=32)
    convergence = max(abs(a-b)/a for a,b in zip(fine,coarse))
    if convergence > 0.005:
        raise ValueError('Independent optical/source quadrature has not converged')
    width, height, pixels = read_ppm(args.image)
    target = []
    for row in range(height):
        for col in range(width):
            target.extend(integral.reference(.02*((col+.5)/width-.5), .02*(.5-(row+.5)/height)))
    channels = []
    for c in range(3):
        got, expected = pixels[c::3], target[c::3]
        mean, reference = sum(got)/len(got), sum(expected)/len(expected)
        channels.append({'mean':mean, 'reference_mean':reference, 'relative_bias':(mean-reference)/reference,
            'relative_pixel_rms':math.sqrt(sum((a-b)**2 for a,b in zip(got,expected))/len(got))/reference,
            'mean_pass':abs(mean-reference)<=max(2/65535,.03*reference)})
    result = {'relative_reference_convergence':convergence, 'channels':channels,
              'mean_pass':all(c['mean_pass'] for c in channels)}
    text = json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text)
    if not result['mean_pass']:raise SystemExit(1)
