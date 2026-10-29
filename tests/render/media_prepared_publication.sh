#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; case=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_publication.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 Declare=Case="$case" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    for field in 'Classic Draws' 'Classic Coeffs' 'Classic Calls' 'Modifier Fallbacks'; do
        if ! grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" "$name.log"; then
            cat "$name.log"; exit 1
        fi
    done
}
check() {
    name=$1; case=$2
    python3 "$SRCDIR/tests/render/volume_joint_media_reference.py" --image "$name.ppm" --case "$case" \
        --output "$name-reference.json"
}
render constant 0
check constant 0
render scalar 1
check scalar 1
render serial 2
check serial 0
render scalar_no_eager 1 -we
render serial_no_eager 2 -we
PYTHONPATH="$SRCDIR/tests/render" python3 - <<'PY'
from pathlib import Path
from volume_transport_reference import read_ppm
for name in ['scalar', 'serial']:
    assert read_ppm(Path(name + '.ppm')) == read_ppm(Path(name + '_no_eager.ppm'))
PY
if "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_publication.pov" +w8 +h8 +pr -a -d -p -v -gp \
    +wt2 -f Declare=Case=3 > context.log 2>&1; then
    cat context.log; exit 1
fi
grep -Fq 'Prepared density field requires ray context and cannot be published.' context.log
if grep -Eq 'Rendering\.\.\.|Rendered [0-9]+|Rendering line' context.log; then
    cat context.log; exit 1
fi
