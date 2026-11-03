#!/bin/sh
set -eu
trap 'rm -f ssnormal_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_normals.pov" -d -p -a +PR +fp16 +w64 +h64 +wt2 +o"ssnormal_$name.ppm" "$@" >"ssnormal_$name.log" 2>&1
    tail -c 24576 "ssnormal_$name.ppm" >"ssnormal_$name.px"
}
render flat Declare=Bump=0
render zero Declare=Strength=0
render pores
render mapped Declare=Mapped=1
render extreme Declare=Strength=20
render quality +q5
render qualityflat +q5 Declare=Bump=0
cmp ssnormal_quality.px ssnormal_qualityflat.px
python3 - <<'PY'
from array import array
from pathlib import Path
import sys

def pixels(name):
    values = array('H', Path('ssnormal_'+name+'.px').read_bytes())
    if sys.byteorder == 'little': values.byteswap()
    return values
flat = pixels('flat')
assert max(abs(a-b) for a,b in zip(flat,pixels('zero'))) == 0, 'zero bump changed flat transport'
assert any(a != b for a,b in zip(flat,pixels('pores'))), 'entry normals had no effect'
for name in ('pores','mapped','extreme'):
    image = pixels(name)
    assert sum(image) > sum(flat)*0.25, name+' lost most light'
    assert sum(image) < sum(flat)*3, name+' gained excessive light'
print('subsurface_normals: flat identity, UV, extreme slopes and low-quality smoke passed')
PY
