#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
sum() {
    name=$1; shift
    [ -f "progressive_receivers_$name.sum" ] && { cat "progressive_receivers_$name.sum"; return; }
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_receivers.pov" +w64 +h48 -a -d -p -v -gp +wt2 \
        +fp16 File_Gamma=1 +o"progressive_receivers_$name.ppm" "$@" \
        > "progressive_receivers_$name.log" 2>&1 || { cat "progressive_receivers_$name.log"; exit 1; }
    tail -c 18432 "progressive_receivers_$name.ppm" | od -An -v -t u1 | awk '
        {for(i=1;i<=NF;i++) {if(++n%2) hi=$i; else sum+=256*hi+$i}} END {print sum}' | tee "progressive_receivers_$name.sum"
}
channels() {
    sum "$@" > /dev/null
    tail -c 18432 "progressive_receivers_$1.ppm" | od -An -v -t u1 | awk '
        {for(i=1;i<=NF;i++) {if(++n%2) hi=$i; else c[(n/2-1)%3]+=256*hi+$i}} END {print c[0], c[1], c[2]}'
}
near() {
    awk -v what="$1" -v a="$2" -v b="$3" -v share="$4" 'BEGIN {
        if (!(b > 0 && a > (1 - share) * b && a < (1 + share) * b)) { print what, a, b > "/dev/stderr"; exit 1 } }'
}
for receiver in 0 1 2 3 4 5 6; do
    near "receiver $receiver against classic" "$(sum "r$receiver" Declare=Receiver=$receiver)" \
        "$(sum "r${receiver}_classic" Declare=Receiver=$receiver Declare=Method=1)" 0.05
done
near "smooth mesh against faceted" "$(sum r1 Declare=Receiver=1)" "$(sum r0 Declare=Receiver=0)" 0.02
near "smooth height_field against faceted" "$(sum r3 Declare=Receiver=3)" "$(sum r2 Declare=Receiver=2)" 0.02
near "floor of 32 by 32 boxes against one box" "$(sum r7 Declare=Receiver=7)" "$(sum r8 Declare=Receiver=8)" 0.02
near "isosurface against tight accuracy" "$(sum r4 Declare=Receiver=4)" "$(sum r4_tight Declare=Receiver=4 Declare=Accuracy=0.00002)" 0.03
near "parametric against tight accuracy" "$(sum r5 Declare=Receiver=5)" "$(sum r5_tight Declare=Receiver=5 Declare=Accuracy=0.0001)" 0.03
irid=$(channels irid Declare=Irid=1)
irid_classic=$(channels irid_classic Declare=Irid=1 Declare=Method=1)
plain=$(channels r0 Declare=Receiver=0)
for c in 1 2 3; do
    near "irid channel $c against classic" "$(echo $irid | cut -d' ' -f$c)" "$(echo $irid_classic | cut -d' ' -f$c)" 0.05
done
awk -v a="$(echo $irid | cut -d' ' -f2)" -v b="$(echo $plain | cut -d' ' -f2)" 'BEGIN { if (!(a < 0.9 * b)) { print "irid leaves photon light", a, b > "/dev/stderr"; exit 1 } }'
rm -f progressive_receivers_*.ppm progressive_receivers_*.log progressive_receivers_*.sum progressive_receivers_*.pov-state
echo 'progressive_receivers: smooth mesh, smooth height_field, isosurface, parametric and bicubic_patch receivers match classic and their exact forms, a tiled floor matches one box, as does irid'
