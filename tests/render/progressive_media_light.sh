#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_media_light.pov" +w32 +h32 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_media_$name.ppm" Declare=PhotonQuality=0.25 "$@" \
        > "progressive_media_$name.log" 2>&1 || { cat "progressive_media_$name.log"; exit 1; }
    tail -c 6144 "progressive_media_$name.ppm" > "progressive_media_$name.px"
}
mean() {
    od -An -v -t u1 "progressive_media_$1.px" | awk '
        {for(i=1;i<=NF;i++) {if(++n%2) hi=$i; else sum+=256*hi+$i}}
        END {print sum/(n/2)/65535}'
}
near() { awk -v a="$1" -v b="$2" -v tolerance="$3" 'BEGIN {r=a/b; exit !(r>1-tolerance && r<1+tolerance)}'; }
render volume +wt1
render threads +wt4
cmp progressive_media_volume.px progressive_media_threads.px
grep -q 'Surface photons stored:' progressive_media_volume.log
! grep -q 'ignored by photon method 1' progressive_media_volume.log
render direct Declare=Photons=0 +wt1
near "$(mean volume)" "$(mean direct)" 0.06
render point Declare=PointLight=1 +wt1
near "$(mean volume)" "$(mean point)" 0.06
render brighter Declare=Brightness=2 +wt2
near "$(mean brighter)" "$(awk -v x="$(mean volume)" 'BEGIN {print 2*x}')" 0.01
render absorbed Declare=Absorption=4 +wt2
awk -v a="$(mean absorbed)" -v b="$(mean volume)" 'BEGIN {exit !(a<b && a>0)}'
render disabled Declare=SourceRefraction=0 +wt2
! grep -q 'Surface photons stored:' progressive_media_disabled.log
render lens Declare=LensIor=1.5 +wt2
render hot_lens Declare=LensIor=1.5 Declare=Heat=0.1 +wt2
! cmp -s progressive_media_lens.px progressive_media_hot_lens.px
render many Declare=Emitters=16 +wt2
near "$(mean many)" "$(mean volume)" 0.08
rm -f progressive_media_*.ppm progressive_media_*.px progressive_media_*.log
echo 'progressive_media_light: emission weights, direct agreement, source controls, self-attenuation, heat and multiple emitters passed'
