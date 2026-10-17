#!/bin/sh
# progressive_photon_file.sh <povray> <srcdir>: method 2 save_file and load_file reuse the shot photons, or fail clearly.
set -eu
POVRAY=$1
SRCDIR=$2
SCENE="$SRCDIR/tests/render/progressive_photons.pov"
FILE=progressive_photons.photons
render() {
    name=$1; shift
    "$POVRAY" +i"$SCENE" +w48 +h32 -a -d -p -v -gp +fp16 File_Gamma=1 +o"progressive_photon_file_$name.ppm" \
        Declare=PhotonQuality=0.25 "$@" > "progressive_photon_file_$name.log" 2>&1 ||
        { cat "progressive_photon_file_$name.log"; exit 1; }
    tail -c 9216 "progressive_photon_file_$name.ppm" > "progressive_photon_file_$name.px"
}
refuse() {
    name=$1; expected=$2; shift 2
    if "$POVRAY" +i"$SCENE" +w8 +h8 -a -d -p -v -f "$@" Declare=PhotonQuality=0.25 Declare=PhotonFile=2 \
        > "progressive_photon_file_$name.log" 2>&1; then
        echo "progressive_photon_file: $name was loaded" >&2
        exit 1
    fi
    grep -q "$expected" "progressive_photon_file_$name.log" ||
        { cat "progressive_photon_file_$name.log"; exit 1; }
}
for feature in Mist=1 Subsurface=1 Dispersion=1.03; do
    feature_name=${feature%%=*}
    rm -f "$FILE"
    render "$feature_name-plain" Declare="$feature" +wt2
    render "$feature_name-saved" Declare="$feature" Declare=PhotonFile=1 +wt4
    test -s "$FILE"
    render "$feature_name-loaded" Declare="$feature" Declare=PhotonFile=2 +wt1
    cmp "progressive_photon_file_$feature_name-plain.px" "progressive_photon_file_$feature_name-saved.px"
    cmp "progressive_photon_file_$feature_name-saved.px" "progressive_photon_file_$feature_name-loaded.px"
done
tr '\n' ' ' < progressive_photon_file_Dispersion-loaded.log | tr -s ' ' | grep -q 'Progressive photons: [^;]* [1-9][0-9]* deposits'
render turned Declare=Dispersion=1.03 Declare=CameraTurn=20 +wt2
render turned_loaded Declare=Dispersion=1.03 Declare=CameraTurn=20 Declare=PhotonFile=2 +wt2
cmp progressive_photon_file_turned.px progressive_photon_file_turned_loaded.px
! cmp -s progressive_photon_file_turned.px progressive_photon_file_Dispersion-loaded.px
refuse classic_reads_new 'Could not load photon map' Declare=Method=1
refuse quality 'other photon settings' Declare=Dispersion=1.03 Declare=PhotonQuality=0.5
refuse shape 'saved for a scene with' Declare=Dispersion=1.03 Declare=InactiveLights=2
refuse weight 'other photon settings' Declare=Dispersion=1.03 Declare=TargetWeight=2
cp "$FILE" progressive_photon_file.whole
bytes=$(wc -c < progressive_photon_file.whole)
head -c $((bytes - 20)) progressive_photon_file.whole > "$FILE"
refuse truncated 'truncated or damaged' Declare=Dispersion=1.03
head -c $((bytes / 2)) progressive_photon_file.whole > "$FILE"
refuse halved 'truncated or damaged' Declare=Dispersion=1.03
rm -f "$FILE"
refuse missing 'Cannot open the photon file' Declare=Dispersion=1.03
render classic_saved Declare=Method=1 Declare=PhotonFile=1 +wt2
refuse classic 'not a photon method 2 file'
render classic_loaded Declare=Method=1 Declare=PhotonFile=2 +wt2
cmp progressive_photon_file_classic_saved.px progressive_photon_file_classic_loaded.px
rm -f "$FILE" progressive_photon_file.whole progressive_photon_file_*.ppm progressive_photon_file_*.px progressive_photon_file_*.log
echo 'progressive_photon_file: saved and loaded maps, moved camera, mismatch, truncation and classic files passed'
