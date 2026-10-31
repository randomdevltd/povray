#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_emission_gi.pov" +w12 +h12 +wt1 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render appearance_only Declare=Glow=50
render lit_low Declare=ExplicitLight=1 Declare=Sigma=0.25
render lit_high Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Glow=50
render modified_low Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Modified=1
render modified_high Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Modified=1 Declare=Glow=50
render scatter_none Declare=Spot=1
render scatter_on Declare=Spot=1 Declare=Sigma=0.5
render scatter_no_gi Declare=Spot=1 Declare=Sigma=0.5 Declare=Radio=0
render visible_low Declare=ViewGlow=1 Declare=Radio=0 Declare=Glow=0.01
render visible_high Declare=ViewGlow=1 Declare=Radio=0 Declare=Glow=0.1
python3 - "$SRCDIR/tests/render" <<'PY'
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
from pathlib import Path
from volume_transport_reference import read_ppm
def pixels(name):
    return read_ppm(Path(name + '.ppm'))[2]
def mean(name):
    values = pixels(name)
    return sum(values) / len(values)
for name in ['appearance_only', 'scatter_none', 'scatter_no_gi']:
    assert max(pixels(name)) == 0, (name, mean(name))
for first, second in [('lit_low', 'lit_high'), ('modified_low', 'modified_high')]:
    a, b = pixels(first), pixels(second)
    assert a == b, (first, second, mean(first), mean(second))
assert mean('scatter_on') > 0.0001, ('physical scattered light did not reach the gather', mean('scatter_on'))
assert mean('visible_low') > 0 and mean('visible_high') > 5 * mean('visible_low')
print('media_emission_gi: appearance excluded from gathering, physical scattering retained, visible glow preserved')
PY
