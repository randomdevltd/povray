#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; width=$2; height=$3; shift 3
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_surface.pov" +w"$width" +h"$height" -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_surface_$name.ppm" "$@" \
        > "progressive_surface_$name.log" 2>&1 || { cat "progressive_surface_$name.log"; exit 1; }
}
render one 64 48 +wt1
render four 64 48 +wt4
tail -c 18432 progressive_surface_one.ppm > progressive_surface_one.px
tail -c 18432 progressive_surface_four.ppm > progressive_surface_four.px
cmp progressive_surface_one.px progressive_surface_four.px
render reference 64 48 +wt4 +ss7 Declare=PhotonQuality=16
python3 "$SRCDIR/tools/bench/check-progressive-photons.py" progressive_surface_reference.ppm \
    progressive_surface_one.ppm > progressive_surface_quality.json
python3 - <<'PY'
import json
from pathlib import Path
result = json.loads(Path('progressive_surface_quality.json').read_text())
assert result['mse'] < 6e-6, result
assert 0.85 < result['energy_ratio'] < 1.15, result
assert result['peak'] < 0.99, result
print('progressive surface reference:', result)
PY
back() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_diffuse_back.pov" +w48 +h48 -a -d -p -v -gp +wt2 \
        +fp16 File_Gamma=1 +o"progressive_surface_$name.ppm" "$@" \
        > "progressive_surface_$name.log" 2>&1 || { cat "progressive_surface_$name.log"; exit 1; }
    tail -c 13824 "progressive_surface_$name.ppm" | od -An -v -t u1 | awk '
        {for(i=1;i<=NF;i++) {if(++n%2) hi=$i; else sum+=256*hi+$i}} END {print sum}'
}
front=$(back front Declare=Front=0.8 Declare=Back=0)
behind=$(back behind Declare=Below=1)
unlit=$(back unlit Declare=Below=1 Declare=Back=0 Declare=Front=0.8)
awk -v f="$front" -v b="$behind" -v u="$unlit" 'BEGIN {
    if (!(f > 0 && u == 0 && b > 0.98 * f && b < 1.02 * f)) { print "diffuse_back photon light", f, b, u > "/dev/stderr"; exit 1 } }'
double=$(back double Declare=Below=1 Declare=Back=0 Declare=Front=0.8 Declare=Double=1)
back double_one Declare=Below=1 Declare=Back=0 Declare=Front=0.8 Declare=Double=1 +wt1 > /dev/null
for name in double double_one behind; do tail -c 13824 "progressive_surface_$name.ppm" > "progressive_surface_$name.px"; done
cmp progressive_surface_double.px progressive_surface_behind.px
cmp progressive_surface_double.px progressive_surface_double_one.px
[ "$double" = "$front" ] || { echo "double_illuminate photon light $front $double" >&2; exit 1; }
rm -f progressive_surface_*.ppm progressive_surface_*.log progressive_surface_*.px progressive_surface_*.pov-state progressive_surface_quality.json
echo 'progressive_surface: close and layered receivers, thread equality, reference quality, diffuse_back and double_illuminate passed'
