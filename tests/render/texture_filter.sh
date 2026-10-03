#!/bin/sh
# texture_filter.sh <povray> <srcdir>: +TF off changes nothing; on, a minified checker nears a supersampled render and a half-filtering layer keeps its mean colour.
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
rm -f tf_*.ppm tf_*.px tf_a.txt tf_b.txt
echo "texture_filter: +TF filters a minified checker and a filtering layer, changes nothing when off, the same on any thread count"
