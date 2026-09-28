#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess


def render(binary, output, case, method, intervals, opaque=False):
    scene = pathlib.Path(__file__).with_name("media-functions.pov")
    include = binary.parent.parent / "distribution" / "include"
    command = [str(binary), f"+I{scene}", f"+L{include}", f"+O{output}",
               "+FP16", "+W32", "+H24", "+WT1", "-A", "-D",
               f"Declare=Case={case}", f"Declare=Method={method}",
               f"Declare=Intervals={intervals}"]
    if opaque:
        command.extend(["Declare=AbsorptionScale=4000", "Declare=LightPower=1000"])
    with output.with_suffix(".log").open("wb") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    with output.open("rb") as stream:
        assert stream.readline().strip() == b"P6"
        fields = []
        while len(fields) < 3:
            line = stream.readline()
            if not line.startswith(b"#"):
                fields.extend(line.split())
        assert fields == [b"32", b"24", b"65535"]
        return struct.unpack(">2304H", stream.read())


parser = argparse.ArgumentParser(description="Compare function-density shadows against a reference renderer.")
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
failures = []
count = 0
maximum = 0
for case in range(16):
    for method, intervals in ((1, 1), (2, 3), (3, 1), (3, 2)):
        tag = f"case-{case}-m{method}-i{intervals}"
        images = [render(binary.resolve(), args.output / f"{tag}-{label}.ppm", case, method, intervals)
                  for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
        error = max(abs(a - b) for a, b in zip(*images))
        maximum = max(maximum, error)
        count += 1
        print(tag, "maximum 16-bit error", error, flush=True)
        tolerance = 0 if case in (9, 14, 15) else 64
        if error > tolerance:
            failures.append((tag, error))
for case in (0, 1):
    for method in (1, 2, 3):
        tag = f"opaque-case-{case}-m{method}"
        images = [render(binary.resolve(), args.output / f"{tag}-{label}.ppm", case, method, 1, True)
                  for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
        error = max(abs(a - b) for a, b in zip(*images))
        maximum = max(maximum, error)
        count += 1
        print(tag, "maximum 16-bit error", error, flush=True)
        if error:
            failures.append((tag, error))
print(f"{count} image pairs, maximum error {maximum}, {len(failures)} failures", failures)
raise SystemExit(bool(failures))
