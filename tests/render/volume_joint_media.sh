#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_joint_media.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
check() {
    name=$1; shift
    python3 "$SRCDIR/tests/render/volume_joint_media_reference.py" --image "$name.ppm" "$@" \
        --output "$name-reference.json"
}
render homogeneous Declare=Case=0
check homogeneous --case 0
render heterogeneous Declare=Case=1
check heterogeneous --case 1
render multiply Declare=Case=2
check multiply --case 2
render plate Declare=Case=3
check plate --case 3
render phase Declare=Case=0 Declare=Phase=0.4
check phase --case 0 --phase 0.4
render subdivision Declare=Case=0 Declare=Resolution=0.025
check subdivision --case 0
python3 - <<'PY'
import json
from pathlib import Path
first = json.loads(Path('homogeneous-reference.json').read_text())
second = json.loads(Path('subdivision-reference.json').read_text())
for a, b in zip(first['channels'], second['channels']):
    assert abs(a['mean'] - b['mean']) <= max(2 / 65535, 0.03 * a['reference_mean'])
PY
