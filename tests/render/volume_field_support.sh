#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for value in 1 2 3 4 5 6; do
    "$POVRAY" +i"$SRCDIR/tests/render/volume_field_support.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Declare=Case="$value" +o"$value.ppm" > "$value.log" 2>&1 || { cat "$value.log"; exit 1; }
done
python3 - "$SRCDIR/tests/render" <<'PY'
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
from pathlib import Path
from volume_transport_reference import compare, read_ppm
for first, second, label in [(1, 2, 'empty container padding'), (3, 4, 'open and closed occupancy'),
                             (5, 6, 'explicit and clipped box')]:
    _, _, expected = read_ppm(Path(f'{first}.ppm'))
    _, _, actual = read_ppm(Path(f'{second}.ppm'))
    compare(label, actual, expected, 0.10)
PY
