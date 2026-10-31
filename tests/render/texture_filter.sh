#!/bin/sh
# texture_filter.sh <povray> <srcdir>: -TF changes nothing; +TF brings minified checkers, bumps, iridescent reflective bumps and a sky seen in bumps near a supersampled render, a sky under a filtering layer keeping its tint, each opt-out leaves its kind unfiltered, and a half-filtering layer keeps its mean colour.
set -e
W=160; H=120
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/texture_filter.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp +fp +o"tf_$name.ppm" "$@"
    tail -c $((W * H * 3)) "tf_$name.ppm" > "tf_$name.px"
}
same() {
    cmp -s "tf_$1.px" "tf_$2.px"
}
# mean absolute difference of two renders, in thousandths of full scale
mad() {
    od -An -v -tu1 "tf_$1.px" | tr -s ' ' '\n' | grep . > tf_a.txt
    od -An -v -tu1 "tf_$2.px" | tr -s ' ' '\n' | grep . > tf_b.txt
    paste tf_a.txt tf_b.txt | awk '{ d = $1 - $2; s += (d < 0) ? -d : d } END { printf "%d", s * 1000 / (NR * 255) }'
}
# mean of one channel (0 red, 1 green, 2 blue) of a linear render, in thousandths of full scale
chmean() {
    od -An -v -tu1 "tf_$1.px" | tr -s ' ' '\n' | grep . | awk -v c=$2 '(NR - 1) % 3 == c { s += $1; n++ } END { printf "%d", s * 1000 / (n * 255) }'
}
POVRAY=$1; SRCDIR=$2
render plain -a
render off -a -tf
render offscale -a Texture_Filter=off Texture_Filter_Scale=2 Texture_Filter_Taps=3
render ref +a0.0 +am1 +r4 -j
render tf8 -a +tf +wt1
render tf8wt4 -a +tf +wt4
render tf3 -a +tf Texture_Filter_Taps=3
same plain off || { echo "texture_filter: -TF changed the image"; exit 1; }
same plain offscale || { echo "texture_filter: filter settings changed the image with the filter off"; exit 1; }
same tf8 tf8wt4 || { echo "texture_filter: the filtered image depends on the thread count"; exit 1; }
if same plain tf8 || same plain tf3; then
    echo "texture_filter: +TF left the image unchanged"; exit 1
fi
off=$(mad plain ref); tf8=$(mad tf8 ref); tf3=$(mad tf3 ref)
echo "texture_filter: error against 16 samples per pixel: off $off, 8 taps $tf8, 3 taps $tf3"
if [ $((tf8 * 2)) -ge "$off" ] || [ $((tf3 * 2)) -ge "$off" ]; then
    echo "texture_filter: +TF did not halve the error of the minified checker"; exit 1
fi
render lay -a Declare=LAYERED=1 File_Gamma=1
render laytf8 -a +tf Declare=LAYERED=1 File_Gamma=1
render laytf3 -a +tf Texture_Filter_Taps=3 Declare=LAYERED=1 File_Gamma=1
for run in laytf8 laytf3; do
    for c in 0 1 2; do
        a=$(chmean lay $c); b=$(chmean $run $c); d=$((a - b))
        if [ ${d#-} -gt 10 ]; then
            echo "texture_filter: $run shifted the mean of channel $c of the filtering layer from $a to $b"; exit 1
        fi
    done
done
"$POVRAY" +i"$SRCDIR/tests/render/texture_filter_uv.pov" +L"$SRCDIR/include" +w$W +h$H -a -d -p -v -gp +fp +o"tf_uv_off.ppm"
"$POVRAY" +i"$SRCDIR/tests/render/texture_filter_uv.pov" +L"$SRCDIR/include" +w$W +h$H -a -d -p -v -gp +fp +tf +wt1 +o"tf_uv_on.ppm"
tail -c $((W * H * 3)) tf_uv_off.ppm > tf_uv_off.px
tail -c $((W * H * 3)) tf_uv_on.ppm > tf_uv_on.px
same uv_off uv_on && { echo "texture_filter: +TF left a UV-mapped texture unchanged"; exit 1; }
"$POVRAY" +i"$SRCDIR/tests/render/texture_filter_screen.pov" +L"$SRCDIR/include" +w$W +h$H -a -d -p -v -gp +fp +o"tf_screen_off.ppm"
"$POVRAY" +i"$SRCDIR/tests/render/texture_filter_screen.pov" +L"$SRCDIR/include" +w$W +h$H -a -d -p -v -gp +fp +tf +wt1 +o"tf_screen_on.ppm"
tail -c $((W * H * 3)) tf_screen_off.ppm > tf_screen_off.px
tail -c $((W * H * 3)) tf_screen_on.ppm > tf_screen_on.px
same screen_off screen_on && { echo "texture_filter: a screen did not carry its footprint into its camera"; exit 1; }
nrender() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/texture_filter_normal.pov" +w$W +h$H -d -p -v -gp +fp +o"tf_$name.ppm" "$@"
    tail -c $((W * H * 3)) "tf_$name.ppm" > "tf_$name.px"
}
render pigoff -a +tf Texture_Filter_Pigments=off
same plain pigoff || { echo "texture_filter: Texture_Filter_Pigments=off still filtered the checker"; exit 1; }
nrender nplain -a
nrender noff -a -tf
nrender nnorm -a +tf Texture_Filter_Normals=off
nrender nref +a0.0 +am1 +r4 -j
nrender ntf -a +tf +wt1
nrender ntfwt4 -a +tf +wt4
nrender npig -a +tf Texture_Filter_Pigments=off
same nplain noff || { echo "texture_filter: -TF changed the bumpy image"; exit 1; }
same nplain nnorm || { echo "texture_filter: Texture_Filter_Normals=off still filtered the normals"; exit 1; }
same ntf npig || { echo "texture_filter: pigment filtering changed the filtered normals of a plain pigment"; exit 1; }
same ntf ntfwt4 || { echo "texture_filter: the filtered normals depend on the thread count"; exit 1; }
noff=$(mad nplain nref); ntf=$(mad ntf nref)
echo "texture_filter: bumpy checker error against 16 samples per pixel: off $noff, filtered $ntf"
if [ $((ntf * 2)) -ge "$noff" ]; then
    echo "texture_filter: +TF did not halve the error of the minified bumps"; exit 1
fi
irender() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/texture_filter_irid.pov" +w$W +h$H -d -p -v -gp +fp File_Gamma=1 +o"tf_$name.ppm" "$@"
    tail -c $((W * H * 3)) "tf_$name.ppm" > "tf_$name.px"
}
irender iplain -a
irender iref +a0.0 +am1 +r4 -j
irender itf -a +tf +wt1
irender itfwt4 -a +tf +wt4
same itf itfwt4 || { echo "texture_filter: filtered iridescence and reflection depend on the thread count"; exit 1; }
ioff=$(mad iplain iref); itf=$(mad itf iref)
echo "texture_filter: iridescent reflective bumps error against 16 samples per pixel: off $ioff, filtered $itf"
if [ $((itf * 4)) -ge $((ioff * 3)) ]; then
    echo "texture_filter: +TF did not cut the error of iridescent reflective bumps by a quarter"; exit 1
fi
for c in 0 1 2; do
    a=$(chmean iref $c); b=$(chmean itf $c); d=$((a - b))
    if [ $((${d#-} * 100)) -gt $((a * 3)) ]; then
        echo "texture_filter: +TF moved the mean of channel $c of the iridescent bumps from $a to $b"; exit 1
    fi
done
srender() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/texture_filter_sky.pov" +w$W +h$H -d -p -v -gp +fp File_Gamma=1 +o"tf_$name.ppm" "$@"
    tail -c $((W * H * 3)) "tf_$name.ppm" > "tf_$name.px"
}
srender splain -a
srender sref +a0.0 +am1 +r4 -j
srender stf -a +tf +wt1
srender stfwt4 -a +tf +wt4
srender snosky -a +tf +wt1 Texture_Filter_Pigments=off
same stf stfwt4 || { echo "texture_filter: the sky seen in bumps depends on the thread count"; exit 1; }
soff=$(mad splain sref); snosky=$(mad snosky sref); stf=$(mad stf sref)
echo "texture_filter: sky in a bumpy mirror error against 16 samples per pixel: off $soff, normals only $snosky, filtered $stf"
if [ $((stf * 3)) -ge $((snosky * 2)) ]; then
    echo "texture_filter: filtering the sky did not cut the error of a bumpy mirror by a third"; exit 1
fi
for c in 0 1 2; do
    a=$(chmean sref $c); b=$(chmean stf $c); d=$((a - b))
    if [ $((${d#-} * 100)) -gt $((a * 8)) ]; then
        echo "texture_filter: +TF moved the mean of channel $c of the sky in bumps from $a to $b"; exit 1
    fi
done
srender fref +a0.0 +am1 +r4 -j Declare=RGBF=1
srender ftf -a +tf +wt1 Declare=RGBF=1
# what the filtering layer passes, as a share of the bare sky in the same render, apart from how +TF moves the sky in bumps
for c in 0 1; do
    a=$(( $(chmean fref $c) * 1000 / $(chmean sref $c) )); b=$(( $(chmean ftf $c) * 1000 / $(chmean stf $c) )); d=$((a - b))
    if [ $((${d#-} * 100)) -gt $((a * 8)) ]; then
        echo "texture_filter: +TF moved channel $c of a sky under a filtering layer, as a share of the bare sky, from $a to $b (x1000)"; exit 1
    fi
done
rm -f tf_*.ppm tf_*.px tf_a.txt tf_b.txt
echo "texture_filter: +TF filters spatial, layered and UV-mapped pigments and normals, iridescence and reflectivity over bumps, the sky, and carries through screens"
