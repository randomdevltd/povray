#!/usr/bin/env python3
import argparse
import math
import pathlib
import struct


def read_ppm(path):
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise ValueError(f"{path}: expected binary PPM")
        fields = []
        while len(fields) < 3:
            line = stream.readline()
            if not line.startswith(b"#"):
                fields.extend(line.split())
        width, height, maximum = map(int, fields)
        if maximum != 65535:
            raise ValueError(f"{path}: expected +FP16")
        data = stream.read()
        samples = width * height * 3
        if len(data) != samples * 2:
            raise ValueError(f"{path}: truncated image")
        return (width, height), struct.unpack(f">{samples}H", data)


parser = argparse.ArgumentParser(description="Report RGB error between two 16-bit PPM renders.")
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
args = parser.parse_args()
shape_a, a = read_ppm(args.reference)
shape_b, b = read_ppm(args.candidate)
if shape_a != shape_b:
    parser.error(f"image sizes differ: {shape_a} and {shape_b}")
errors = [abs(x - y) for x, y in zip(a, b)]
pixels = [max(errors[i:i + 3]) for i in range(0, len(errors), 3)]
print(f"size {shape_a[0]}x{shape_a[1]}")
print(f"mean_channel {sum(errors) / len(errors):.3f} / 65535")
print(f"rms_channel {math.sqrt(sum(x * x for x in errors) / len(errors)):.3f} / 65535")
print(f"max_channel {max(errors)} / 65535")
for limit in (64, 256, 1024, 4096):
    print(f"pixels_above_{limit} {sum(x > limit for x in pixels)} / {len(pixels)}")
