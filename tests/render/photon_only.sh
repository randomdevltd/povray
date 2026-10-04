#!/bin/sh
set -e
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/photon_only.pov" +w96 +h72 -a -d -p -v -gp +fp16 +o"photon_only_$name.ppm" "$@" > "photon_only_$name.log" 2>&1 ||
        { cat "photon_only_$name.log"; exit 1; }
    tail -c 41472 "photon_only_$name.ppm" > "photon_only_$name.px"
}
render dark Declare=Lamp=0
render isolated Declare=PhotonOnly=1
render direct Declare=PhotonOnly=0
render photons Declare=PhotonOnly=1 Declare=Photons=1
render dark_fill Declare=Lamp=0 Declare=MixedLights=1
render isolated_fill Declare=PhotonOnly=1 Declare=MixedLights=1
cmp photon_only_dark.px photon_only_isolated.px
cmp photon_only_dark_fill.px photon_only_isolated_fill.px
! cmp -s photon_only_dark.px photon_only_direct.px
! cmp -s photon_only_dark.px photon_only_photons.px
sed -n 's/^Surface photons stored: *\([0-9][0-9]*\).*/\1/p' photon_only_photons.log | awk '$1 > 0 { found=1 } END { exit !found }'
rm -f photon_only_*.ppm photon_only_*.px photon_only_*.log
echo "photon_only: photons remain while direct light is absent"
