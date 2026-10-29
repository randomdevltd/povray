#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_joint_roles.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
for kind in 0 1 2; do
    render "role$kind" Declare=Case="$kind"
    python3 "$SRCDIR/tests/render/volume_joint_roles_reference.py" --case "$kind" --image "role$kind.ppm"
done
render refraction Declare=Case=3
render refraction_control Declare=Case=3 Declare=Control=1
python3 "$SRCDIR/tests/render/volume_joint_roles_reference.py" --case 3 --image refraction.ppm --control refraction_control.ppm
python3 - <<'PY'
import re
from pathlib import Path
for path in Path('.').glob('*.log'):
    text = path.read_text()
    draws = re.search(r'Media Fixed Draws:\s+(\d+)\s+Classic Draws:\s+(\d+)', text)
    pairs = re.search(r'Media Scatter Legs:\s+(\d+)\s+Source Pairs:\s+(\d+)', text)
    assert draws and int(draws[2]) == 0, path
    assert pairs and int(pairs[2]) > 0, path
PY
