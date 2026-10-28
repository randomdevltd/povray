#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for native in 0 1; do
    "$POVRAY" +i"$SRCDIR/tests/render/refraction_clear_axis.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Native="$native" +o"clear$native.ppm" \
        > "clear$native.log" 2>&1 || { cat "clear$native.log"; exit 1; }
done
PYTHONPATH="$SRCDIR/tests/render" python3 - <<'PY'
from pathlib import Path
from volume_transport_reference import read_ppm
from refraction_prepared_ray_reference import ray
for native in [0,1]:
    w,h,pixels=read_ppm(Path('clear'+str(native)+'.ppm'))
    errors=[]
    for i in range(w*h):
        x0=.1*((i%w+.5)/w-.5)
        red,green=pixels[3*i:3*i+2]
        assert green>0
        actual=(red/green-.5)/.2
        errors.append(abs(actual-ray(x0,bool(native))['screen_x']))
    print('native',native,'maximum coordinate error',max(errors))
    assert max(errors)<=.001
PY
