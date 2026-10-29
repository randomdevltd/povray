#!/bin/sh
# progressive_photon_file.sh <povray> <srcdir>: method 2 save_file and load_file reuse the shot photons, or fail clearly.
set -eu
POVRAY=$1
SRCDIR=$2
SCENE="$SRCDIR/tests/render/progressive_photons.pov"
FILE=progressive_photons.photons
POV_PHOTON_MEMORY=64
export POV_PHOTON_MEMORY
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
    tr '\n' ' ' < "progressive_photon_file_$name.log" | tr -s ' ' | grep -q "$expected" ||
        { cat "progressive_photon_file_$name.log"; exit 1; }
}
for feature in Mist=1 Subsurface=1 VolumeSource=1 VolumeSource=2 VolumeSource=3 Dispersion=1.03; do
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
if cmp -s progressive_photon_file_turned.px progressive_photon_file_Dispersion-loaded.px; then
    echo 'progressive_photon_file: the turned camera rendered the same image' >&2
    exit 1
fi
refuse classic_reads_new 'Could not load photon map' Declare=Method=1
refuse quality 'other photon settings' Declare=Dispersion=1.03 Declare=PhotonQuality=0.5
refuse shape 'saved for a scene with' Declare=Dispersion=1.03 Declare=InactiveLights=2
refuse weight 'other photon settings' Declare=Dispersion=1.03 Declare=TargetWeight=2
too_big() {
    name=$1; source=$2; shift 2
    refuse "budget_$name" "needs at least [0-9.e-]* GB for photons, more than the 1e-05 GB allowed (set by the $source)" \
        Declare=Dispersion=1.03 "$@"
}
(POV_PHOTON_MEMORY=0.00001; too_big env 'POV_PHOTON_MEMORY environment variable')
too_big option 'Photon_Memory option' Photon_Memory=0.00001
(unset POV_PHOTON_MEMORY; too_big scene "scene's photons memory" Declare=PhotonMemory=0.00001)
(unset POV_PHOTON_MEMORY; render budget_option_up Declare=Dispersion=1.03 Declare=PhotonFile=2 Declare=PhotonMemory=0.00001 Photon_Memory=64 +wt2)
cmp progressive_photon_file_Dispersion-loaded.px progressive_photon_file_budget_option_up.px
cp "$FILE" progressive_photon_file.whole
python3 - progressive_photon_file.whole "$FILE" <<'PYOLD'
import struct, sys
data = bytearray(open(sys.argv[1], 'rb').read())
struct.pack_into('=I', data, 8, 2)
open(sys.argv[2], 'wb').write(data)
PYOLD
refuse old_version 'format version 2; this build reads version 3' Declare=Dispersion=1.03
python3 - progressive_photon_file.whole "$FILE" <<'PYRECEIVER'
import struct, sys
data = bytearray(open(sys.argv[1], 'rb').read())
at = 12
def skip_string():
    global at
    at += 4 + struct.unpack_from('=I', data, at)[0]
skip_string(); at += 8; skip_string(); at += 4; skip_string()
struct.pack_into('=I', data, at, (1 << 29) + 1)
open(sys.argv[2], 'wb').write(data)
PYRECEIVER
refuse receiver_overflow 'truncated or damaged' Declare=Dispersion=1.03
bytes=$(wc -c < progressive_photon_file.whole)
head -c $((bytes - 20)) progressive_photon_file.whole > "$FILE"
refuse truncated 'truncated or damaged' Declare=Dispersion=1.03
head -c $((bytes / 2)) progressive_photon_file.whole > "$FILE"
refuse halved 'truncated or damaged' Declare=Dispersion=1.03
# a NaN in the first surface photon's point is refused by name, not sorted
python3 - progressive_photon_file.whole "$FILE" <<'PY'
import struct, sys
data = bytearray(open(sys.argv[1], 'rb').read())
at = 12
def skip_string():
    global at
    at += 4 + struct.unpack_from('=I', data, at)[0]
skip_string(); at += 8; skip_string(); at += 4; skip_string()
at += 4 + 4 * struct.unpack_from('=I', data, at)[0]
assert struct.unpack_from('=Q', data, at)[0] > 0
struct.pack_into('=d', data, at + 8, float('nan'))
open(sys.argv[2], 'wb').write(data)
PY
refuse nan 'non-finite surface photon point' Declare=Dispersion=1.03
rm -f "$FILE"
refuse missing 'Cannot open the photon file' Declare=Dispersion=1.03
render classic_saved Declare=Method=1 Declare=PhotonFile=1 +wt2
refuse classic 'not a photon method 2 file'
render classic_loaded Declare=Method=1 Declare=PhotonFile=2 +wt2
cmp progressive_photon_file_classic_saved.px progressive_photon_file_classic_loaded.px
rm -f "$FILE" progressive_photon_file.whole progressive_photon_file_*.ppm progressive_photon_file_*.px progressive_photon_file_*.log
echo 'progressive_photon_file: saved and loaded maps, moved camera, mismatch, memory ceilings from each source, truncation, non-finite values and classic files passed'
