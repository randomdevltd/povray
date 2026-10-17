#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
SCENE="$SRCDIR/tests/render/progressive_photons.pov"
POV_PHOTON_MEMORY=64
export POV_PHOTON_MEMORY
render() {
    name=$1; shift
    "$POVRAY" +i"$SCENE" +w48 +h32 -a -d -p -v -gp +fp16 File_Gamma=1 +o"progressive_photons_$name.ppm" \
        Declare=PhotonQuality=0.25 "$@" > "progressive_photons_$name.log" 2>&1 ||
        { cat "progressive_photons_$name.log"; exit 1; }
    tail -c 9216 "progressive_photons_$name.ppm" > "progressive_photons_$name.px"
}
lit() {
    od -An -v -t u1 "progressive_photons_$1.px" | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
}
budget() {
    tr '\n' ' ' < "progressive_photons_$1.log" | tr -s ' ' | grep -o 'Progressive photons: [^;]*deposits, k [0-9]*'
}
render one +wt1
render four +wt4
cmp progressive_photons_one.px progressive_photons_four.px
render inactive Declare=InactiveLights=8 +wt2
cmp progressive_photons_one.px progressive_photons_inactive.px
for lamp in 1 2 3 4; do
    render "lamp$lamp-one" Declare=LampType="$lamp" +wt1
    render "lamp$lamp-four" Declare=LampType="$lamp" +wt4
    cmp "progressive_photons_lamp$lamp-one.px" "progressive_photons_lamp$lamp-four.px"
done
for feature in Mist=1 Dispersion=1.03 Subsurface=1 Thin=1 Nearby=1 ViewMode=1 ViewMode=2 Curved=1; do
    feature_name=${feature%%=*}
    render "$feature_name-one" Declare="$feature" +wt1
    render "$feature_name-four" Declare="$feature" +wt3
    cmp "progressive_photons_$feature_name-one.px" "progressive_photons_$feature_name-four.px"
done
render volume_only Declare=Mist=1 Declare=OnlyVolume=1 Declare=PhotonOnly=1 +wt2
render subsurface_only Declare=Subsurface=1 Declare=PhotonOnly=1 +wt2
lit volume_only
lit subsurface_only
render aa_one +a0.05 +am2 +r2 +wt1
render aa_four +a0.05 +am2 +r2 +wt4
cmp progressive_photons_aa_one.px progressive_photons_aa_four.px
grep -q 'Samples: *[1-9]' progressive_photons_aa_one.log
: > progressive_photons_progressive.pov-state
ln -f progressive_photons_progressive.pov-state progressive_photons_saved.pov-state
render progressive +pr +wt2 +a0.05 +am4 Declare=PhotonOnly=1
state_bytes=$(wc -c < progressive_photons_saved.pov-state)
head -c $((state_bytes - 100)) progressive_photons_saved.pov-state > progressive_photons_resumed.pov-state
render resumed +pr +wt4 +a0.05 +am4 Declare=PhotonOnly=1 +c
cmp progressive_photons_progressive.px progressive_photons_resumed.px
render legacy Declare=Method=0 +wt1
render classic Declare=Method=1 +wt1
cmp progressive_photons_legacy.px progressive_photons_classic.px
render no_glow Declare=TestGlow=1 Declare=SourceGlow=0 Declare=PhotonOnly=1 +wt2
render glow Declare=TestGlow=1 Declare=SourceGlow=100 Declare=PhotonOnly=1 +wt2
cmp progressive_photons_no_glow.px progressive_photons_glow.px
render power1 Declare=PhotonOnly=1 Declare=LampPower=2 +wt2
render power2 Declare=PhotonOnly=1 Declare=LampPower=4 +wt2
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
"$POVRAY" +i"$SCENE" +w4000 +h2667 +sc2000 +ec2000 +sr1333 +er1333 -a -d -p -v -f Declare=PhotonQuality=0.25 +wt2 \
    > progressive_photons_large.log 2>&1 || { cat progressive_photons_large.log; exit 1; }
[ "$(budget one)" = "$(budget large)" ] || { echo "photon budget depends on the image size" >&2; budget one; budget large; exit 1; }
for quality in 0 -1 16384.5; do
    if "$POVRAY" +i"$SCENE" +w1 +h1 -a -d -p -v -f Declare=PhotonQuality="$quality" \
        > progressive_photons_invalid.log 2>&1; then
        echo "invalid photon quality accepted: $quality" >&2
        exit 1
    fi
    grep -q 'quality must be' progressive_photons_invalid.log
done
refused() {
    tr '\n' ' ' < "progressive_photons_$1.log" | tr -s ' ' | grep -q "$2" || { cat "progressive_photons_$1.log"; exit 1; }
}
if (unset POV_PHOTON_MEMORY; "$POVRAY" +i"$SCENE" +w1 +h1 -a -d -p -v -f Declare=PhotonQuality=16384 +wt2) \
    > progressive_photons_memory.log 2>&1; then
    echo "photon quality beyond the default memory budget accepted" >&2
    exit 1
fi
refused memory 'after batch 1), more than the [0-9.]* GB allowed (40% of available memory, at most 6 GB)'
ceiling() {
    name=$1; expected=$2; shift 2
    if "$POVRAY" +i"$SCENE" +w8 +h8 -a -d -p -v -f Declare=PhotonQuality=0.25 +wt2 "$@" \
        > "progressive_photons_$name.log" 2>&1; then
        [ "$expected" = accepted ] || { echo "photons accepted under the memory ceiling ($name)" >&2; exit 1; }
    elif [ "$expected" = accepted ]; then
        cat "progressive_photons_$name.log"; exit 1
    else
        refused "$name" "$expected"
    fi
}
overrides() {
    refused "$1" "Photon memory ceiling $2 GB (set by the $3) overrides the scene's photons memory $4 GB"
}
too_much='after batch 1), more than the [0-9.]* GB allowed'
for value in 0.001 abc 0 -1 nan; do
    case $value in
        0.001) env_expected="$too_much (set by the POV_PHOTON_MEMORY environment variable)"
               option_expected="$too_much (set by the Photon_Memory option)" ;;
        *) env_expected='POV_PHOTON_MEMORY must be a number of gigabytes'
           option_expected='Photon_Memory must be a number of gigabytes greater than zero' ;;
    esac
    (POV_PHOTON_MEMORY=$value; ceiling "env_$value" "$env_expected")
    ceiling "option_$value" "$option_expected" Photon_Memory="$value"
done
for value in 1e-50 1e39; do
    ceiling "option_$value" 'Photon_Memory must be a number of gigabytes greater than zero and within' Photon_Memory="$value"
done
(POV_PHOTON_MEMORY=abc; ceiling env_bad_option_ok 'POV_PHOTON_MEMORY must be a number of gigabytes' Photon_Memory=64)
(
    unset POV_PHOTON_MEMORY
    ceiling scene "$too_much (set by the scene's photons memory)" Declare=PhotonMemory=0.001
    ceiling scene_generous accepted Declare=PhotonMemory=64
    if grep -q 'overrides the scene' progressive_photons_scene_generous.log; then
        cat progressive_photons_scene_generous.log; exit 1
    fi
    ceiling option_up accepted Declare=PhotonMemory=0.001 Photon_Memory=64
    overrides option_up 64 'Photon_Memory option' 0.001
    ceiling option_down "$too_much (set by the Photon_Memory option)" Declare=PhotonMemory=64 Photon_Memory=0.001
    overrides option_down 0.001 'Photon_Memory option' 64
    for value in 0 -1; do
        ceiling "scene_$value" 'Photon memory must be a finite number of gigabytes greater than zero' Declare=PhotonMemory="$value"
        refused "scene_$value" "photons.pov' line [0-9]*: Parse Error: Photon memory"
    done
    ceiling scene_abc "line [0-9]*: Parse Error: Expected 'numeric expression', string" Declare=PhotonMemory='"abc"'
    ceiling method1 'Photon memory requires method 2' Declare=Method=1 Declare=PhotonMemory=1
)
ceiling env_over_scene accepted Declare=PhotonMemory=0.001
overrides env_over_scene 64 'POV_PHOTON_MEMORY environment variable' 0.001
ceiling option_over_env "$too_much (set by the Photon_Memory option)" Declare=PhotonMemory=64 Photon_Memory=0.001
(POV_PHOTON_MEMORY=0.001; ceiling option_over_tiny_env accepted Photon_Memory=64)
cat > progressive_photons_nan.pov <<'SCENE'
#version 3.8;
#declare Big = 1e300 * 1e300;
global_settings { photons { method 2 memory Big - Big } }
sphere { 0, 1 }
SCENE
(SCENE=progressive_photons_nan.pov; ceiling scene_nan 'Photon memory must be a finite number of gigabytes greater than zero')
"$POVRAY" +i"$SCENE" +w8 +h8 -a -d -p -v -f Declare=PhotonQuality=0.25 +wt2 > progressive_photons_ceiling.log 2>&1 ||
    { cat progressive_photons_ceiling.log; exit 1; }
rm -f progressive_photons_*.ppm progressive_photons_*.px progressive_photons_*.pov-state progressive_photons_*.log progressive_photons_nan.pov
echo 'progressive_photons: deterministic threads, lamps and features, anti-aliasing, resume, classic compatibility, emission power, size-independent budget, memory budget, ceilings from the scene, Photon_Memory and POV_PHOTON_MEMORY and their precedence, and invalid quality and ceilings passed'
