#!/usr/bin/env python3
import argparse
import json
import math
import re
import struct
from array import array
from pathlib import Path


def pixels(path):
    with Path(path).open('rb') as stream:
        signature = stream.readline()
        if signature.strip() == b'P6':
            stream.seek(0)
            data = stream.read()
            header = re.match(rb'P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+65535\s', data)
            if header is None:
                raise ValueError('expected linear 16-bit PPM')
            width, height = map(int, header.groups())
            raw = data[header.end():]
            if len(raw) != width * height * 6:
                raise ValueError('unexpected PPM length')
            values = struct.unpack('>' + str(width * height * 3) + 'H', raw)
            return width, height, array('f', (value / 65535 for value in values))
        if signature.strip() != b'#?RADIANCE':
            raise ValueError('expected Radiance HDR or linear PPM')
        while True:
            line = stream.readline()
            if not line:
                raise ValueError('missing HDR dimensions')
            if line.startswith(b'-Y '):
                break
        dimensions = re.fullmatch(rb'-Y (\d+) \+X (\d+)\s*', line)
        if dimensions is None:
            raise ValueError('unsupported HDR orientation')
        height, width = map(int, dimensions.groups())
        output = array('f')
        for row in range(height):
            if stream.read(4) != bytes((2, 2, width >> 8, width & 255)):
                raise ValueError('expected modern HDR scanline encoding')
            channels = []
            for channel in range(4):
                values = bytearray()
                while len(values) < width:
                    code = stream.read(1)
                    if not code or code[0] == 0:
                        raise ValueError('invalid HDR run')
                    count = code[0]
                    if count > 128:
                        value = stream.read(1)
                        if not value:
                            raise ValueError('truncated HDR run')
                        values.extend(value * (count - 128))
                    else:
                        values.extend(stream.read(count))
                    if len(values) > width:
                        raise ValueError('HDR run crosses scanline')
                channels.append(values)
            for x in range(width):
                exponent = channels[3][x]
                for channel in range(3):
                    output.append(math.ldexp(channels[channel][x] + 0.5, exponent - 136) if exponent else 0.0)
        return width, height, output


def reduce_reference(image, width, height):
    rw, rh, values = image
    if rw % width or rh % height or rw < width or rh < height:
        raise ValueError('reference must be an integer supersample of the candidate')
    sx, sy = rw // width, rh // height
    result = [0.0] * (width * height * 3)
    for y in range(rh):
        for x in range(rw):
            source = 3 * (y * rw + x)
            target = 3 * ((y // sy) * width + x // sx)
            for channel in range(3):
                result[target + channel] += values[source + channel]
    return [value / (sx * sy) for value in result]


def main():
    parser = argparse.ArgumentParser(description='Compare linear photon renders with an optionally supersampled HDR reference.')
    parser.add_argument('reference')
    parser.add_argument('candidates', nargs='+')
    parser.add_argument('--size', nargs=2, type=int, metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--rect', nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'))
    args = parser.parse_args()
    reference = pixels(args.reference)
    reduced = {}
    for candidate in args.candidates:
        width, height, values = pixels(candidate)
        if args.size:
            values = reduce_reference((width, height, values), *args.size)
            width, height = args.size
        if (width, height) not in reduced:
            reduced[width, height] = reduce_reference(reference, width, height)
        expected = reduced[width, height]
        if args.rect:
            x0, y0, x1, y1 = args.rect
            if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
                raise ValueError('rectangle must lie inside the compared image')
            indices = [3 * (y * width + x) + c for y in range(y0, y1) for x in range(x0, x1) for c in range(3)]
            values = [values[i] for i in indices]
            expected = [expected[i] for i in indices]
        mse = sum((a - b) ** 2 for a, b in zip(values, expected)) / len(values)
        mean = sum(values) / len(values)
        reference_mean = sum(expected) / len(expected)
        print(json.dumps(dict(candidate=Path(candidate).name, mse=mse, rms=math.sqrt(mse),
                              mean=mean, reference_mean=reference_mean,
                              energy_ratio=mean / reference_mean if reference_mean else None,
                              peak=max(values), reference_peak=max(expected))))


if __name__ == '__main__':
    main()
