#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_volume.pov" +w24 +h24 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_volume_$name.ppm" "$@" \
        > "progressive_volume_$name.log" 2>&1 || { cat "progressive_volume_$name.log"; exit 1; }
    tail -c 3456 "progressive_volume_$name.ppm" > "progressive_volume_$name.px"
}
mean() {
    od -An -v -t u1 "progressive_volume_$1.px" | awk '
        {for(i=1;i<=NF;i++) {if(++n%2) hi=$i; else sum+=256*hi+$i}}
        END {print sum/(n/2)/65535}'
}
render prepared +wt1
tr '\n' ' ' < progressive_volume_prepared.log | tr -s ' ' | grep -q 'Progressive photons: 1 emission batches'
render classic Declare=ReceiverMethod=3 +wt1
awk -v a="$(mean prepared)" -v b="$(mean classic)" 'BEGIN {exit !(a>0.65*b && a<1.35*b)}'
render threads +wt4
cmp progressive_volume_prepared.px progressive_volume_threads.px
render straight Declare=Heat=0 +wt2
! cmp -s progressive_volume_prepared.px progressive_volume_straight.px
render varying Declare=Variation=0.1 +wt2
! cmp -s progressive_volume_prepared.px progressive_volume_varying.px
stored() {
    tr '\n' ' ' < "progressive_volume_$1.log" | tr -s ' ' | grep -o 'Media photons stored: [0-9]*' | grep -o '[0-9]*$'
}
render spaced Declare=Variation=0.1 Declare=MediaSpacing=4 +wt2
[ "$(stored spaced)" -lt "$(stored varying)" ] || { echo 'progressive_volume: the media spacing factor changes no step' >&2; exit 1; }
awk -v a="$(mean spaced)" -v b="$(mean varying)" 'BEGIN {exit !(a>0.97*b && a<1.03*b)}'
render decoy_first Declare=Variation=0.1 Declare=Decoy=1 +wt2
render decoy_last Declare=Variation=0.1 Declare=Decoy=2 +wt2
awk -v a="$(stored decoy_first)" -v b="$(stored decoy_last)" 'BEGIN {exit !(a>0.98*b && a<1.02*b)}' ||
    { echo 'progressive_volume: media steps depend on the order of targets' >&2; exit 1; }
render brighter Declare=LampPower=2 +wt2
awk -v a="$(mean prepared)" -v b="$(mean brighter)" 'BEGIN {exit !(b>1.98*a && b<2.02*a)}'
render group Declare=Grouped=1 +wt2
contributions() {
    tr '\n' ' ' < "progressive_volume_$1.log" | tr -s ' ' | grep -o '[0-9]* contributions'
}
[ "$(contributions group)" = "$(contributions prepared)" ] ||
    { echo 'progressive_volume: grouped media receive other photons than ungrouped ones' >&2; exit 1; }
grouped_render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_volume.pov" +w3000 +h2667 +pr -a -d -p -v -gp \
        +sc1492 +ec1507 +sr1326 +er1341 Declare=PhotonQuality=1.25 Declare=Heat=0 \
        +fp16 File_Gamma=1 +o"progressive_volume_grouped_$name.ppm" "$@" \
        > "progressive_volume_grouped_$name.log" 2>&1 || { cat "progressive_volume_grouped_$name.log"; exit 1; }
}
grouped_render one Declare=LampPower=0.1 +wt1
grouped_render four Declare=LampPower=0.1 +wt4
grouped_render brighter Declare=LampPower=0.2 +wt4
python3 - <<'PY_GROUPED'
from array import array
from pathlib import Path
import re
import sys

def pixels(name):
    raw = Path('progressive_volume_grouped_' + name + '.ppm').read_bytes()
    header = re.match(rb'P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+65535\s', raw)
    assert header is not None
    values = array('H')
    values.frombytes(raw[header.end():])
    if sys.byteorder == 'little':
        values.byteswap()
    return values

one, four, brighter = pixels('one'), pixels('four'), pixels('brighter')
assert one == four
assert sum(one) > 100 and max(brighter) < 65535
assert 1.98 < sum(brighter) / sum(one) < 2.02
PY_GROUPED
printf 'progressive_volume means: prepared=%s classic=%s\n' "$(mean prepared)" "$(mean classic)"
rm -f progressive_volume_*.ppm progressive_volume_*.px progressive_volume_*.log
echo 'progressive_volume: prepared photon gathers, curved segments, extinction, media spacing, target steps, power, threads and grouped media passed'
