#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess


def pixels(path):
    with path.open("rb") as stream:
        assert stream.readline().strip() == b"P6"
        fields = []
        while len(fields) < 3:
            line = stream.readline()
            if not line.startswith(b"#"):
                fields.extend(line.split())
        assert fields == [b"32", b"24", b"65535"]
        data = stream.read()
        assert len(data) == 32 * 24 * 3 * 2
        return struct.unpack(">2304H", data)


def render(binary, output, options, scene_name="media-opacity.pov"):
    scene = pathlib.Path(__file__).with_name(scene_name)
    include = binary.parent.parent / "distribution" / "include"
    command = [str(binary), f"+I{scene}", f"+L{include}", f"+O{output}",
               "+FP16", "+W32", "+H24", "+WT1", "-A", "-D"]
    command.extend(f"Declare={key}={value}" for key, value in options.items())
    with output.with_suffix(".log").open("wb") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    return pixels(output)


parser = argparse.ArgumentParser(description="Compare opacity-cutoff shadows against a reference renderer.")
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
cases = [("opaque", 2, 1, 40), ("below", 1.10, 1, 40),
         ("edge", 1.1552453, 1, 40), ("above", 1.16, 1, 40),
         ("varying", 4, 0, 40), ("small", 2, 1, 3),
         ("many", 0.2, 1, 10000)]
failures = []
count = 0
for method in (1, 2, 3):
    for intervals in (1, 2, 3):
        for name, absorption, density, samples in cases:
            tag = f"{name}-m{method}-i{intervals}"
            options = dict(Method=method, Intervals=intervals, Absorption=absorption,
                           StartDensity=density, Samples=samples)
            images = [render(binary.resolve(), args.output / f"{tag}-{label}.ppm", options)
                      for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
            error = max(abs(a - b) for a, b in zip(*images))
            count += 1
            if error:
                failures.append((tag, error))
    options = dict(Method=method, Asymmetric=1)
    images = [render(binary.resolve(), args.output / f"rgb-m{method}-{label}.ppm", options)
              for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
    error = max(abs(a - b) for a, b in zip(*images))
    count += 1
    if error:
        failures.append((f"rgb-m{method}", error))
for segments in (1, 8, 128, 512):
    for area, atmosphere in ((0, 0), (1, 0), (1, 1)):
        tag = f"segments-{segments}-area-{area}-atmosphere-{atmosphere}"
        options = dict(Segments=segments, Area=area, Atmosphere=atmosphere)
        images = [render(binary.resolve(), args.output / f"{tag}-{label}.ppm", options, "media-segments.pov")
                  for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
        error = max(abs(a - b) for a, b in zip(*images))
        count += 1
        print(tag, "maximum 16-bit error", error)
        if error > 64:
            failures.append((tag, error))
print(f"{count} image pairs, {len(failures)} failures", failures)
raise SystemExit(bool(failures))
