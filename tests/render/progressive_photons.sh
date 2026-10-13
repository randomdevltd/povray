#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_photons.pov" +w48 +h32 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_photons_$name.ppm" Declare=PhotonQuality=0.125 "$@" \
        > "progressive_photons_$name.log" 2>&1 || { cat "progressive_photons_$name.log"; exit 1; }
    tail -c 9216 "progressive_photons_$name.ppm" > "progressive_photons_$name.px"
}
: > progressive_photons_one.pov-state
ln -f progressive_photons_one.pov-state progressive_photons_saved.pov-state
render one +wt1
render four +wt4
cmp progressive_photons_one.px progressive_photons_four.px
render inactive Declare=InactiveLights=8 +wt2
cmp progressive_photons_one.px progressive_photons_inactive.px
state_bytes=$(wc -c < progressive_photons_saved.pov-state)
head -c $((state_bytes - 100)) progressive_photons_saved.pov-state > progressive_photons_resumed.pov-state
rm -f progressive_photons_resumed.ppm
render resumed +wt4 +c
cmp progressive_photons_one.px progressive_photons_resumed.px
for lamp in 1 2 3 4; do
    render "lamp$lamp-one" Declare=LampType="$lamp" +wt1
    render "lamp$lamp-four" Declare=LampType="$lamp" +wt4
    cmp "progressive_photons_lamp$lamp-one.px" "progressive_photons_lamp$lamp-four.px"
done
for feature in Mist=1 Dispersion=1.03 Subsurface=1 Thin=1 Nearby=1 ViewMode=1 ViewMode=2 Curved=1; do
    feature_name=${feature%%=*}
    render "$feature_name-one" Declare="$feature" +wt1
    render "$feature_name-four" Declare="$feature" +wt4
    cmp "progressive_photons_$feature_name-one.px" "progressive_photons_$feature_name-four.px"
done
render volume_only Declare=Mist=1 Declare=OnlyVolume=1 Declare=PhotonOnly=1 +wt2
render subsurface_only Declare=Subsurface=1 Declare=PhotonOnly=1 +wt2
for name in volume_only subsurface_only; do
    od -An -v -t u1 "progressive_photons_$name.px" | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
done
render legacy Declare=Method=0 +wt1
render classic Declare=Method=1 +wt1
cmp progressive_photons_legacy.px progressive_photons_classic.px
render no_glow Declare=TestGlow=1 Declare=SourceGlow=0 Declare=PhotonOnly=1 +wt2
render glow Declare=TestGlow=1 Declare=SourceGlow=100 Declare=PhotonOnly=1 +wt2
cmp progressive_photons_no_glow.px progressive_photons_glow.px
render power1 Declare=PhotonOnly=1 Declare=LampPower=0.1 +wt2
render power2 Declare=PhotonOnly=1 Declare=LampPower=0.2 +wt2
od -An -v -t u1 progressive_photons_power1.px progressive_photons_power2.px | awk '
    { for (i=1; i<=NF; i++) {
        if (++bytes % 2) upper=$i;
        else {
            value=256*upper+$i;
            if (++channels<=4608) low+=value;
            else { high+=value; if(value==65535) clipped=1; }
        }
    } }
    END { if (low<=100 || clipped || high/low<1.99 || high/low>2.01) {
        print "photon-only power is empty, clipped or nonlinear" > "/dev/stderr"; exit 1;
    } }'
for quality in 0 -1 257; do
    if "$POVRAY" +i"$SRCDIR/tests/render/progressive_photons.pov" +w1 +h1 +pr -a -d -p -v -f \
        Declare=PhotonQuality="$quality" > progressive_photons_invalid.log 2>&1; then
        echo "invalid photon quality accepted: $quality" >&2
        exit 1
    fi
    grep -q 'quality must be finite' progressive_photons_invalid.log
done
rm -f progressive_photons_*.ppm progressive_photons_*.px progressive_photons_*.pov-state progressive_photons_*.log
echo 'progressive_photons: deterministic threads and resume, classic compatibility, emission power and invalid budgets passed'
