#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
for source in 0 1 2 3 4; do
    disabled=1
    if [ "$source" -eq 4 ]; then disabled=0; fi
    for method in 1 2; do
        "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 +o"progressive_direct_${source}_${method}.ppm" \
            Declare=Source="$source" Declare=Method="$method" Declare=Disabled="$disabled" +wt1 \
            > "progressive_direct_${source}_${method}.log" 2>&1 || { cat "progressive_direct_${source}_${method}.log"; exit 1; }
    done
done
for source in 0 1 2 3 4; do
    tail -c 384 "progressive_direct_${source}_1.ppm" > progressive_direct_classic.px
    tail -c 384 "progressive_direct_${source}_2.ppm" > progressive_direct_progressive.px
    od -An -v -t u1 progressive_direct_classic.px progressive_direct_progressive.px | awk '
        {for(i=1;i<=NF;i++) {
            if(++bytes%2) hi=$i;
            else {
                value=256*hi+$i;
                if(++channel<=192) {original[channel]=value; total+=value;}
                else {difference=value-original[channel-192]; if(difference>2 || difference< -2) failed=1;}
            }
        }}
        END {exit failed || total<=100}'
done
# with photons on but no target, method 2 leaves every light's direct power and fade as authored
for fade in 0 1 2 3 4 5; do
    for method in 1 2; do
        "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 +o"progressive_direct_fade${fade}_${method}.ppm" \
            Declare=Fade="$fade" Declare=Method="$method" Declare=Disabled=0 +wt1 \
            > "progressive_direct_fade${fade}_${method}.log" 2>&1 || { cat "progressive_direct_fade${fade}_${method}.log"; exit 1; }
    done
    tail -c 384 "progressive_direct_fade${fade}_1.ppm" > progressive_direct_classic.px
    tail -c 384 "progressive_direct_fade${fade}_2.ppm" > progressive_direct_progressive.px
    cmp progressive_direct_classic.px progressive_direct_progressive.px
    od -An -v -t u1 progressive_direct_progressive.px | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
done
# photons through an ior 1 target deliver the direct light's irradiance at any distance, on surfaces and in media
view() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_direct_$name.ppm" Declare=Disabled=0 "$@" > "progressive_direct_$name.log" 2>&1 ||
        { cat "progressive_direct_$name.log"; exit 1; }
}
for distance in 5 245; do
    for fade in 0 1 2 3 4 5; do
        view "near${fade}_$distance" +w8 +h8 +wt2 Declare=Fade="$fade" Declare=Distance="$distance"
        view "lens${fade}_$distance" +w8 +h8 +wt2 Declare=Fade="$fade" Declare=Distance="$distance" Declare=Lens=1
    done
    for fade in 0 1; do
        view "mediumnear${fade}_$distance" +w8 +h8 +wt2 Declare=Fade="$fade" Declare=Distance="$distance" Declare=Media=1
        view "mediumlens${fade}_$distance" +w8 +h8 +wt2 Declare=Fade="$fade" Declare=Distance="$distance" Declare=Media=1 Declare=Lens=1 Declare=PhotonQuality=16
    done
    for method in 1 2; do
        view "glass${method}_$distance" +w32 +h32 +wt2 Declare=Method="$method" Declare=Distance="$distance" Declare=Lens=1 Declare=Glass=1
    done
done
# a medium bending the beam inside the target keeps the whole path length in the deposit weight
view bendnear +w8 +h8 +wt2 Declare=Distance=20 Declare=Bend=1
for bend in 1 2; do
    view "bend$bend" +w8 +h8 +wt2 Declare=Distance=20 Declare=Bend="$bend" Declare=Lens=1 Declare=PhotonQuality=16
done
python3 - <<'PY'
from array import array
from pathlib import Path
import sys

def mean(name):
    data = Path(f'progressive_direct_{name}.ppm').read_bytes()
    pixels = array('H')
    pixels.frombytes(data[data.index(b'65535\n') + 6:])
    if sys.byteorder == 'little':
        pixels.byteswap()
    return sum(pixels) / len(pixels)

for distance in (5, 245):
    cases = [(f'lens{fade}', f'near{fade}', 0.03) for fade in range(6)]
    cases += [(f'mediumlens{fade}', f'mediumnear{fade}', 0.03) for fade in range(2)] + [('glass2', 'glass1', 0.10)]
    for photon, direct, tolerance in cases:
        ratio = mean(f'{photon}_{distance}') / mean(f'{direct}_{distance}')
        print(f'distance {distance}: {photon} / {direct} {ratio:.4f}')
        assert abs(ratio - 1.0) < tolerance, (distance, photon, ratio)
for photon in ('bend1', 'bend2'):
    ratio = mean(photon) / mean('bendnear')
    print(f'distance 20: {photon} / bendnear {ratio:.4f}')
    assert abs(ratio - 1.0) < 0.03, (photon, ratio)
PY
# a refracting target darkens its shadow for photons only when the map holds deposits
for method in 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_direct_blocked_${method}.ppm" Declare=Method="$method" Declare=Disabled=0 Declare=Lens=1 \
        Declare=Blocked=1 +wt2 > progressive_direct_blocked.log 2>&1 || { cat progressive_direct_blocked.log; exit 1; }
done
grep -q 'Surface photons stored: *0' progressive_direct_blocked.log || grep -q ' 0 deposits' progressive_direct_blocked.log ||
    { cat progressive_direct_blocked.log; exit 1; }
tail -c 384 progressive_direct_blocked_1.ppm > progressive_direct_classic.px
tail -c 384 progressive_direct_blocked_2.ppm > progressive_direct_progressive.px
cmp progressive_direct_classic.px progressive_direct_progressive.px
od -An -v -t u1 progressive_direct_progressive.px | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
for source in 2 3; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w1 +h1 +pr -a -d -p -v -f \
        Declare=Source="$source" Declare=Disabled=0 Declare=Power=0 > progressive_direct_zero.log 2>&1 || { cat progressive_direct_zero.log; exit 1; }
done
rm -f progressive_direct_*.px progressive_direct_*.ppm progressive_direct_*.log progressive_direct_*.pov-state
echo 'progressive_direct_lights: direct-light power and fade, photon power calibration at 5 and 245 units on surfaces and in media and through a bending medium, glass caustics against classic, shadows without deposits, disabled and zero emitters passed'
