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
render far_point_diffuse Declare=PointSource=1 Declare=SingleSamples=0
render far_volume_diffuse Declare=SingleSamples=0
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
. "$SRCDIR/tests/render/image.sh"
image_awk '
function pixels(name, v,    n, average) {
    n = read_ppm("sslt_volume_" name ".ppm", v)
    if (IMAGE_W != 12 || IMAGE_H != 12) fail(name ": " IMAGE_W "x" IMAGE_H ", expected 12x12")
    average = sum_of(v, n) / n
    if (!(min_of(v, n) >= 0 && 0.001 < average && average < 0.9))
        fail(name ": minimum " min_of(v, n) ", mean " average ", expected mean in (0.001, 0.9)")
    return n
}
function mean(name,    v) {
    return sum_of(v, pixels(name, v)) / 144.0
}
BEGIN {
    # J = Phi/(4*pi); the classic inverse-square colour is J/pi.
    na = pixels("far_volume", a); nb = pixels("far_point", b)
    compare("SSLT far physical intensity", a, na, b, nb, 0.03)
    # A small distant emitter single scattering, alone, converges to an equal-power point light.
    volume = mean("far_volume") - mean("far_volume_diffuse")
    point = mean("far_point") - mean("far_point_diffuse")
    printf "SSLT far single scattering: volume %.6f, point %.6f\n", volume, point
    if (!(point > 0 && abs(volume / point - 1) < 0.05)) fail("SSLT far single scattering: volume " volume ", point " point ", limit 5%")
    na = pixels("near_photons", a); nb = pixels("near_direct", b)
    compare("SSLT near physical photon irradiance", a, na, b, nb, 0.08)
}'
echo 'subsurface_volume_emitter: physical far-field point equivalence and near-field photon irradiance passed'
render near_reuse Declare=NearSource=1 Declare=SingleSamples=0 Declare=Photons=1 Declare=PhotonQuality=0.25 Declare=ReuseViews=1
tr -d ' \r\n' < sslt_volume_near_reuse.log > sslt_volume_near_reuse.txt
grep -q 'Photonmapsshotonceforthisscene' sslt_volume_near_reuse.txt
count=$(grep -o 'Method2pointcloudatspacing0.3:' sslt_volume_near_reuse.txt | wc -l)
[ "$count" -eq 2 ] || { cat sslt_volume_near_reuse.log; exit 1; }
tail -c 864 sslt_volume_near_reuse-1.ppm > sslt_volume_near_reuse-1.pixels
tail -c 864 sslt_volume_near_reuse-2.ppm > sslt_volume_near_reuse-2.pixels
cmp sslt_volume_near_reuse-1.pixels sslt_volume_near_reuse-2.pixels
echo 'subsurface_volume_emitter: photon reuse preserves subsurface settings and pixels'
