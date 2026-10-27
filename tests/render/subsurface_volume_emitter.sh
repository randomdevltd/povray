#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_volume_emitter.pov" +w12 +h12 +wt2 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"sslt_volume_$name.ppm" "$@" > "sslt_volume_$name.log" 2>&1 ||
        { cat "sslt_volume_$name.log"; exit 1; }
}
render far_point Declare=PointSource=1
render far_volume
render near_direct Declare=NearSource=1 Declare=SingleSamples=0
render near_photons Declare=NearSource=1 Declare=SingleSamples=0 Declare=Photons=1 Declare=PhotonQuality=16
for name in far_point far_volume near_direct near_photons; do
    tr -d ' \r\n' < "sslt_volume_$name.log" | grep -q 'Method2pointcloud:[1-9][0-9]*shadingpointstried,[1-9][0-9]*served' ||
        { cat "sslt_volume_$name.log"; exit 1; }
done
for pattern in 'samples/set), [1-9][0-9]* deposits' ' [1-9][0-9]* contributions;'; do
    tr '\n' ' ' < sslt_volume_near_photons.log | tr -s ' ' | grep -q "$pattern" ||
        { cat sslt_volume_near_photons.log; exit 1; }
done
python3 "$SRCDIR/tests/render/subsurface_volume_emitter_reference.py" .
echo 'subsurface_volume_emitter: physical far-field point equivalence and near-field photon irradiance passed'
