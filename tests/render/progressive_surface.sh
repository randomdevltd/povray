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
rm -f progressive_surface_*.ppm progressive_surface_*.log progressive_surface_*.px progressive_surface_*.pov-state progressive_surface_quality.json
echo 'progressive_surface: close and layered receivers, thread equality and reference quality passed'
