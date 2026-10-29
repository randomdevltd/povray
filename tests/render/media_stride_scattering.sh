#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for fog in 0 0.005 0.01; do
    "$POVRAY" +i"$SRCDIR/tests/render/media_stride_scattering.pov" +w16 +h12 +wt1 -a -d -p -v -gp +fp16 File_Gamma=1 \
        Stochastic_Seed=0 Declare=Fog="$fog" +o"fog$fog.ppm" >"fog$fog.log" 2>&1 || { cat "fog$fog.log"; exit 1; }
    tail -c 1152 "fog$fog.ppm" >"fog$fog.rgb"
done
python3 - <<'PY'
import struct
power = lambda name: sum(struct.unpack('>576H', open(name + '.rgb', 'rb').read()))
base = power('fog0')
ratio = (power('fog0.01') - base) / (power('fog0.005') - base)
assert 1.8 < ratio < 2.2, ratio
PY
echo 'media_stride_scattering: strided single scattering is linear in the scattering coefficient'
