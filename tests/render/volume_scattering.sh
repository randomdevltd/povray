#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_scattering.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render center
render outside Declare=Receiver=1.002
render farther Declare=Receiver=2
render scaled Declare=Radius=2
render surface Declare=Surface=1 Declare=SourceL=1
render tiny_native Declare=Surface=1 Declare=SourceL=1 Declare=Radius=0.00000001
render tiny_transformed Declare=Surface=1 Declare=SourceL=1 Declare=Radius=0.00000001 Declare=Native=0
render hemisphere Declare=Surface=2 Declare=SourceL=1
. "$SRCDIR/tests/render/image.sh"
image_awk -l volume_scattering_reference 'BEGIN { volume_scattering_check(ARGV[1]) }' "$WORK"
