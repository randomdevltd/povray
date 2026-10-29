#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for case in 0 1 2 3 4; do
    "$POVRAY" +i"$SRCDIR/tests/render/interior_axis_media.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Declare=Case="$case" +o"axis$case.ppm" \
        > "axis$case.log" 2>&1 || { cat "axis$case.log"; exit 1; }
done
PYTHONPATH="$SRCDIR/tests/render" python3 - <<'PY'
import math
from pathlib import Path
from volume_transport_reference import read_ppm
for case in range(5):
    _,_,values=read_ppm(Path('axis'+str(case)+'.ppm'))
    depth=2 if case in [2,3] else 3
    transmission=math.exp(-.2*depth)
    emission=0
    if case==4:
        q=math.exp(-.2)
        emission=.02/.2*(1-q)*(1+q/1.1**2+q*q)
    targets=[c*transmission+emission for c in [.4,.6,.2]]
    for c,target in enumerate(targets):
        assert all(abs(x-target)<=max(2/65535,.03*target) for x in values[c::3]),(case,c,target)
    print('interior axis',case,'reference',targets)
PY
