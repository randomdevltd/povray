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
# photons through an ior 1 target at distance t carry the direct light's power there, then spread as 1/r^2
for fade in 0 1 2 3 4 5; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_direct_lens${fade}.ppm" Declare=Fade="$fade" Declare=Disabled=0 Declare=Lens=1 +wt2 \
        > "progressive_direct_lens${fade}.log" 2>&1 || { cat "progressive_direct_lens${fade}.log"; exit 1; }
done
python3 - <<'PY'
from array import array
from pathlib import Path
import math, sys

def mean(name):
    pixels = array('H')
    pixels.frombytes(Path(name).read_bytes()[-384:])
    if sys.byteorder == 'little':
        pixels.byteswap()
    return sum(pixels) / len(pixels)

def fade(d, distance, power):
    return 1.0 if power == 0 else (2.0 / (1.0 + (d / distance) ** power) if distance > 0 else d ** -power)

t, r = math.sqrt(3.25), math.sqrt(13.0)
for index, (distance, power, parallel) in enumerate([(0, 0, 0), (2, 1, 0), (0, 2, 0), (1.5, 3, 0), (1, 2, 1), (1, 1, 1)]):
    expected = 1.0 if parallel else fade(t, distance, power) * t * t / (r * r * fade(r, distance, power))
    ratio = mean(f'progressive_direct_lens{index}.ppm') / mean(f'progressive_direct_fade{index}_2.ppm')
    print(f'fade {index}: photon / direct {ratio:.4f}, expected {expected:.4f}')
    assert abs(ratio / expected - 1.0) < 0.03, (index, ratio, expected)
PY
for source in 2 3; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w1 +h1 +pr -a -d -p -v -f \
        Declare=Source="$source" Declare=Disabled=0 Declare=Power=0 > progressive_direct_zero.log 2>&1 || { cat progressive_direct_zero.log; exit 1; }
done
rm -f progressive_direct_*.px progressive_direct_*.ppm progressive_direct_*.log progressive_direct_*.pov-state
echo 'progressive_direct_lights: direct-light power and fade, photon power calibration, disabled and zero emitters passed'
