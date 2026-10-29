#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
# a fixed photon ceiling, so the budget never reads the host's free memory
POV_PHOTON_MEMORY=4
export POV_PHOTON_MEMORY
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
tr '\n' ' ' < progressive_volume_prepared.log | tr -s ' ' | grep -q 'Progressive photons: 1 shooting batches'
render classic Declare=ReceiverMethod=3 +wt1
awk -v a="$(mean prepared)" -v b="$(mean classic)" 'BEGIN {exit !(a>0.65*b && a<1.35*b)}'
render threads +wt4
cmp progressive_volume_prepared.px progressive_volume_threads.px
render straight Declare=Heat=0 +wt2
if cmp -s progressive_volume_prepared.px progressive_volume_straight.px; then
    echo 'progressive_volume: heat changed nothing' >&2
    exit 1
fi
render varying Declare=Variation=0.1 +wt2
if cmp -s progressive_volume_prepared.px progressive_volume_varying.px; then
    echo 'progressive_volume: variation changed nothing' >&2
    exit 1
fi
stored() {
    tr '\n' ' ' < "progressive_volume_$1.log" | tr -s ' ' | grep -o 'Media photons stored: [0-9]*' | grep -o '[0-9]*$'
}
# prepared detail is the floor of a method 4 march, so only a spacing finer than its cells adds steps
render spaced Declare=Variation=0.1 Declare=MediaSpacing=0.25 Declare=MediaSteps=64 +wt2
[ "$(stored spaced)" -gt "$(stored varying)" ] || { echo 'progressive_volume: the media spacing factor changes no step' >&2; exit 1; }
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
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    n1 = read_samples("progressive_volume_grouped_one.ppm", one)
    n4 = read_samples("progressive_volume_grouped_four.ppm", four)
    nb = read_samples("progressive_volume_grouped_brighter.ppm", brighter)
    if (!same_values(one, n1, four, n4)) fail("progressive_volume: grouped media differ between one and four threads")
    total = sum_of(one, n1)
    if (!(total > 100 && max_of(brighter, nb) < 65535)) fail("progressive_volume: grouped sum " total ", brighter peak " max_of(brighter, nb))
    ratio = sum_of(brighter, nb) / total
    if (!(1.98 < ratio && ratio < 2.02)) fail("progressive_volume: grouped power ratio " ratio ", expected between 1.98 and 2.02")
}'
printf 'progressive_volume means: prepared=%s classic=%s\n' "$(mean prepared)" "$(mean classic)"
rm -f progressive_volume_*.ppm progressive_volume_*.px progressive_volume_*.log
echo 'progressive_volume: prepared photon gathers, curved segments, extinction, media spacing, target steps, power, threads and grouped media passed'
