#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
for source in 0 1 2 3 4; do
    for method in 1 2; do
        "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 +o"progressive_direct_${source}_${method}.ppm" \
            Declare=Source="$source" Declare=Method="$method" +wt1 \
            > "progressive_direct_${source}_${method}.log" 2>&1 || { cat "progressive_direct_${source}_${method}.log"; exit 1; }
    done
done
python3 - <<'PY'
from array import array
from pathlib import Path
import sys

def values(source, method):
    output = array('H')
    output.frombytes(Path(f'progressive_direct_{source}_{method}.ppm').read_bytes()[-384:])
    if sys.byteorder == 'little':
        output.byteswap()
    return output

for source in range(5):
    classic, progressive = values(source, 1), values(source, 2)
    assert sum(classic) > 100
    assert max(abs(a-b) for a, b in zip(classic, progressive)) <= 2
PY
for source in 2 3; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w1 +h1 +pr -a -d -p -v -f \
        Declare=Source="$source" Declare=Disabled=0 Declare=Power=0 > progressive_direct_zero.log 2>&1 || { cat progressive_direct_zero.log; exit 1; }
done
rm -f progressive_direct_*.ppm progressive_direct_*.log progressive_direct_*.pov-state
echo 'progressive_direct_lights: direct-light power, disabled and zero emitters passed'
