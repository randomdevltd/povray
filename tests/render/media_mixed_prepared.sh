#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1
    "$POVRAY" +i"$SRCDIR/tests/render/$name.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render media_mixed_prepared
for field in 'Prepared4 Queries' 'Legacy3 Queries'; do
    grep -Eq "$field:[[:space:]]+[1-9][0-9]*" media_mixed_prepared.log || { cat media_mixed_prepared.log; exit 1; }
done
for field in 'Forbidden4 Procedure' 'Old Integrator'; do
    grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" media_mixed_prepared.log || { cat media_mixed_prepared.log; exit 1; }
done
python3 "$SRCDIR/tests/render/media_mixed_prepared_reference.py" media_mixed_prepared.ppm --output mixed-reference.json
render media_prepared_infinite
PYTHONPATH="$SRCDIR/tests/render" python3 - <<'PY'
import math
from pathlib import Path
from volume_transport_reference import read_ppm
_,_,pixels = read_ppm(Path('media_prepared_infinite.ppm'))
for c,sigma in enumerate([.2,.4,.6]):
    target = math.exp(-2*sigma)
    assert all(abs(value-target)<=max(2/65535,.03*target) for value in pixels[c::3])
PY
grep -Eq 'Prepared4 Queries:[[:space:]]+[1-9][0-9]*' media_prepared_infinite.log
for field in 'Forbidden4 Procedure' 'Old Integrator'; do
    grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" media_prepared_infinite.log || { cat media_prepared_infinite.log; exit 1; }
done
