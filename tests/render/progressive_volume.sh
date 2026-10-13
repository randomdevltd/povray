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
render classic Declare=ReceiverMethod=3 +wt1
awk -v a="$(mean prepared)" -v b="$(mean classic)" 'BEGIN {exit !(a>0.65*b && a<1.35*b)}'
render threads +wt4
cmp progressive_volume_prepared.px progressive_volume_threads.px
render straight Declare=Heat=0 +wt2
! cmp -s progressive_volume_prepared.px progressive_volume_straight.px
render varying Declare=Variation=0.1 +wt2
! cmp -s progressive_volume_prepared.px progressive_volume_varying.px
render brighter Declare=LampPower=2 +wt2
awk -v a="$(mean prepared)" -v b="$(mean brighter)" 'BEGIN {exit !(b>1.98*a && b<2.02*a)}'
if "$POVRAY" +i"$SRCDIR/tests/render/progressive_volume.pov" +w8 +h8 +pr -a -d -p -v -gp -f \
    Declare=Grouped=1 > progressive_volume_group.log 2>&1; then
    echo 'progressive_volume: unsupported grouped volume transport was accepted' >&2
    exit 1
fi
grep -q 'does not yet support volumetric photons from light groups' progressive_volume_group.log
printf 'progressive_volume means: prepared=%s classic=%s\n' "$(mean prepared)" "$(mean classic)"
rm -f progressive_volume_*.ppm progressive_volume_*.px progressive_volume_*.log
echo 'progressive_volume: prepared photon gathers, curved segments, extinction, power, threads and group rejection passed'
