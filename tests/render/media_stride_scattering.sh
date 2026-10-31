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
"$POVRAY" +i"$SRCDIR/tests/render/media_stride_scattering.pov" +w16 +h12 +wt1 -a -d -p -v -gp Declare=Fog=0.01 \
    Declare=LightSamples=2 +o"double.ppm" >double.log 2>&1 || { cat double.log; exit 1; }
draws() { sed -n 's/.*Media Fixed Draws: *\([0-9]*\).*/\1/p' "$1"; }
awk -v a="$(draws fog0.01.log)" -v b="$(draws double.log)" 'BEGIN { r = b / a; if (!(a > 0 && r > 1.8 && r < 2.2)) { print "media_stride_scattering: light_samples 2 drew " b " against " a; exit 1 } }'
if "$POVRAY" +i"$SRCDIR/tests/render/media_stride_scattering.pov" +w4 +h3 -d -p -v -gp Declare=Fog=0.01 Declare=LightSamples=0 \
    +o"zero.ppm" >zero.log 2>&1 || ! grep -q 'light_samples takes a finite multiplier greater than 0' zero.log; then cat zero.log; exit 1; fi
python3 - <<'PY'
import struct
power = lambda name: sum(struct.unpack('>576H', open(name + '.rgb', 'rb').read()))
base = power('fog0')
ratio = (power('fog0.01') - base) / (power('fog0.005') - base)
assert 1.8 < ratio < 2.2, ratio
PY
echo 'media_stride_scattering: single scattering is linear in the scattering coefficient; light_samples scales the volume-light draws'
