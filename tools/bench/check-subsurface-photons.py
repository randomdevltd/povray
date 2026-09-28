#!/usr/bin/env python3
import argparse
import json
import math
import re
import struct
from pathlib import Path


def pixels(path):
    data = Path(path).read_bytes()
    header = re.match(rb'P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+65535\s', data)
    if header is None:
        raise ValueError('expected a linear 16-bit PPM')
    width, height = map(int, header.groups())
    raw = data[header.end():]
    if len(raw) != width * height * 6:
        raise ValueError('unexpected pixel count')
    return width, height, [v / 65535 for v in struct.unpack('>' + str(width * height * 3) + 'H', raw)]


def compare(reference, candidate, rect=None):
    w, h, a = pixels(reference)
    cw, ch, b = pixels(candidate)
    if (w, h) != (cw, ch):
        raise ValueError('image sizes differ')
    x0, y0, x1, y1 = rect or (0, 0, w, h)
    indices = [3 * (y * w + x) + c for y in range(y0, y1) for x in range(x0, x1) for c in range(3)]
    errors = sorted(abs(b[i] - a[i]) for i in indices)
    means = [sum(image[i] for i in indices) / len(indices) for image in (a, b)]
    return dict(reference_mean=means[0], candidate_mean=means[1],
                relative_energy=(means[1] / means[0] if means[0] else None),
                rms=math.sqrt(sum(e * e for e in errors) / len(errors)),
                p99=errors[int(0.99 * (len(errors) - 1))], maximum=errors[-1],
                identical=errors[-1] == 0, candidate_peak=max(b[i] for i in indices))


def main():
    parser = argparse.ArgumentParser(description='Compare linear subsurface fixture images over a receiver rectangle.')
    parser.add_argument('reference')
    parser.add_argument('candidate')
    parser.add_argument('--rect', nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'))
    parser.add_argument('--identical', action='store_true')
    parser.add_argument('--max-relative-energy-error', type=float)
    args = parser.parse_args()
    result = compare(args.reference, args.candidate, args.rect)
    print(json.dumps(result, indent=2))
    if args.identical and not result['identical']:
        raise SystemExit('pixels differ')
    if args.max_relative_energy_error is not None:
        ratio = result['relative_energy']
        if ratio is None or abs(ratio - 1) > args.max_relative_energy_error:
            raise SystemExit('energy differs beyond the requested tolerance')


if __name__ == '__main__':
    main()
