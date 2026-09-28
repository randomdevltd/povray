#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess


def render(binary, output, case, generator, fallback, method):
    scene = pathlib.Path(__file__).with_name("media-function-simd.pov")
    include = binary.parent.parent / "distribution" / "include"
    command = [str(binary), f"+I{scene}", f"+L{include}", f"+O{output}",
               "+FP16", "+W32", "+H24", "+WT1", "-A", "-D",
               f"Declare=Case={case}", f"Declare=Generator={generator}",
               f"Declare=Fallback={fallback}", f"Declare=Method={method}"]
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


parser = argparse.ArgumentParser(description="Check scalar and SIMD function-density rendering for identical pixels.")
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
cases = [(case, generator, 0, method)
         for case in range(9) for generator in (1, 2, 3) for method in (1, 2, 3)]
cases.extend((case, 3, fallback, 3) for case in (0, 3, 4, 8) for fallback in (1, 2))
failures = []
for case, generator, fallback, method in cases:
    tag = f"case-{case}-g{generator}-f{fallback}-m{method}"
    images = [render(binary.resolve(), args.output / f"{tag}-{label}.ppm", case, generator, fallback, method)
              for binary, label in ((args.reference, "reference"), (args.candidate, "candidate"))]
    error = max(abs(a - b) for a, b in zip(*images))
    print(tag, "maximum 16-bit error", error, flush=True)
    if error:
        failures.append((tag, error))
print(f"{len(cases)} image pairs, {len(failures)} failures", failures)
raise SystemExit(bool(failures))
