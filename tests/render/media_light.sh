#!/bin/sh
# media_light.sh <povray> <srcdir>: interior lights against point lights, radiosity, light groups and smoke.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=64
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_light.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"media_light_$name.ppm" "$@" \
        > "media_light_$name.log" 2>&1 || { cat "media_light_$name.log"; exit 1; }
}
# mean <name> <col0> <col1> <row0> <row1>: mean of all channels over a block of pixels, 0..1
mean() {
    tail -c $(( 6 * W * H )) "media_light_$1.ppm" | od -An -v -tu1 -w6 | awk -v w=$W -v c0=$2 -v c1=$3 -v r0=$4 -v r1=$5 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 { for (k = 1; k <= 5; k += 2) { s += ($k * 256 + $(k + 1)) / 65535; n++ } }
        END { printf "%.5f", s / n }'
}
# chan <name> <channel 0-2> <col0> <col1> <row0> <row1>: mean of one channel over a block of pixels
chan() {
    tail -c $(( 6 * W * H )) "media_light_$1.ppm" | od -An -v -tu1 -w6 | awk -v w=$W -v k=$(( 2 * $2 + 1 )) -v c0=$3 -v c1=$4 -v r0=$5 -v r1=$6 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 { s += ($k * 256 + $(k + 1)) / 65535; n++ }
        END { printf "%.5f", s / n }'
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) "media_light_$name.ppm" > "media_light_$name.pixels"; done
    cmp -s "media_light_$1.pixels" "media_light_$2.pixels" || { echo "media_light: $1 differs from $2" >&2; exit 1; }
}
# near <what> <got> <expected> <relative tolerance>
near() {
    awk -v a="$2" -v b="$3" -v t="$4" 'BEGIN { exit !(a - b <= t * b && b - a <= t * b) }' ||
        { echo "media_light: $1 is $2, expected $3 within $4" >&2; exit 1; }
}
says() {
    tr -s ' \n' '  ' < "media_light_$2.log" | grep -q "$1"
}

render ball Declare=Case=1
render point Declare=Case=2
# A glowing gas ball lights like a point at its centre: its radiance times the silhouette over the height squared.
near "the floor under the ball" "$(mean ball 30 33 30 33)" 0.25 0.04
for block in "4 11 4 11" "52 59 28 35" "28 35 52 59" "20 27 36 43"; do
    near "block $block" "$(mean ball $block)" "$(mean point $block)" 0.03
done

render radiosity Declare=Case=3
near "the floor under radiosity" "$(mean radiosity 24 39 24 39)" "$(mean ball 24 39 24 39)" 0.01
render glow Declare=Case=4
# a medium's emission is appearance alone: radiosity gathers none of it
awk -v a="$(mean glow 0 63 0 63)" 'BEGIN { exit !(a == 0) }' ||
    { echo "media_light: an emission-only medium lit the floor through radiosity" >&2; exit 1; }

render group Declare=Case=5
# The camera looks down with z up, so the group's floor, at negative x, is the right half of the image.
near "the light group's floor" "$(mean group 36 59 4 59)" "$(mean ball 36 59 4 59)" 0.01
awk -v a="$(mean group 0 27 0 63)" 'BEGIN { exit !(a == 0) }' ||
    { echo "media_light: a light group's interior light lights the floor outside the group" >&2; exit 1; }

render smoke Declare=Case=6
render clear Declare=Case=7
near "light through half a unit of absorption 1" "$(mean smoke 28 35 28 35)" \
    "$(awk -v a="$(mean clear 28 35 28 35)" 'BEGIN { printf "%.5f", a * exp(-0.5) }')" 0.03

render fine Declare=Case=8
near "64 samples" "$(mean fine 8 55 8 55)" "$(mean point 8 55 8 55)" 0.02

render photons Declare=Case=1 Declare=Photons=1
says "photons in an interior light are ignored" photons || { echo "media_light: no warning for photons in an interior light" >&2; exit 1; }
near "the floor with an ignored photons block" "$(mean photons 8 55 8 55)" "$(mean ball 8 55 8 55)" 0.0001

render dense Declare=Case=9
render dense_ref Declare=Case=9 Declare=N=512
for block in "8 23 8 23" "40 55 8 23" "24 39 24 39" "8 23 40 55"; do
    near "non-uniform density, block $block" "$(mean dense $block)" "$(mean dense_ref $block)" 0.03
done
render colour Declare=Case=10
render colour_ref Declare=Case=10 Declare=N=512
for ch in 0 1 2; do
    near "the light's own pigment, channel $ch" "$(chan colour $ch 8 55 8 55)" "$(chan colour_ref $ch 8 55 8 55)" 0.02
done
render shadow Declare=Case=11 Declare=N=32
render shadow_ref Declare=Case=11 Declare=N=512
for block in "24 39 24 39" "18 25 28 35" "38 45 28 35"; do
    near "soft shadow, block $block" "$(mean shadow $block)" "$(mean shadow_ref $block)" 0.05
done
awk -v a="$(mean shadow 24 39 24 39)" -v b="$(mean ball 24 39 24 39)" 'BEGIN { exit !(a < 0.5 * b) }' ||
    { echo "media_light: the occluder casts no shadow" >&2; exit 1; }
render shadowless Declare=Case=12 Declare=Shadowless=1
render unoccluded Declare=Case=13 Declare=Shadowless=1
same shadowless unoccluded
render copied Declare=Case=14
render direct Declare=Case=15
for block in "8 55 8 55" "28 47 16 35"; do
    near "a copied, scaled light, block $block" "$(mean copied $block)" "$(mean direct $block)" 0.005
done
render cadence Declare=Case=16
render stepped Declare=Case=17
for block in "20 43 20 43" "24 39 24 39"; do
    near "scattering at its own cadence, block $block" "$(mean cadence $block)" "$(mean stepped $block)" 0.03
done
near "scattering at its own cadence, far corner" "$(mean cadence 8 23 8 23)" "$(mean stepped 8 23 8 23)" 0.15
render one Declare=Case=1 +WT1
render three Declare=Case=1 +WT3
same one three
render mesh Declare=Case=18
near "an interior light in a deferred isosurface mesh" "$(mean mesh 30 33 30 33)" "$(mean ball 30 33 30 33)" 0.05
# a pending .povm octahedron holding a light, in an unsplit union whose box was taken before it loaded
le32() { printf "\\$(printf %03o $(($1 & 255)))\\$(printf %03o $(($1 >> 8 & 255)))\\$(printf %03o $(($1 >> 16 & 255)))\\$(printf %03o $(($1 >> 24 & 255)))"; }
{
    printf POVM; for n in 1 0 6 0 0 8; do le32 $n; done
    for v in 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864; do le32 $v; done
    for i in 0 2 4 2 1 4 1 3 4 3 0 4 2 0 5 1 2 5 3 1 5 0 3 5; do le32 $i; done
} > media_light.povm
render held Declare=Case=19 +L.
render alone Declare=Case=20 +L.
for block in "0 63 0 63" "24 39 24 39"; do
    near "a light's mesh in a union, block $block" "$(mean held $block)" "$(mean alone $block)" 0.01
done
render far Declare=Case=21
render farpoint Declare=Case=22
# beyond its far radius the ball is its centre with the same escaped power
near "the far field against an equal-power point light" "$(mean far 28 35 28 35)" "$(mean farpoint 28 35 28 35)" 0.02
near "the far field against the analytic point" "$(mean far 30 33 30 33)" 0.015625 0.04

# a scattering medium is lit by the volume light as by a light of its escaped power: near the
# emitter gentler than that point, the same bounded law a surface receives, and far from it,
# beyond its far radius, the point's equal; the pair would move if the magnitude changed
render smokenear Declare=Case=23 Declare=SmokeHeight=1.2
render smokenearpoint Declare=Case=24 Declare=SmokeHeight=1.2
near "smoke near the ball, against the point of its power" "$(mean smokenear 20 43 20 43)" \
    "$(awk -v a="$(mean smokenearpoint 20 43 20 43)" 'BEGIN { printf "%.5f", a * 0.914 }')" 0.08
render smokefar Declare=Case=23 Declare=SmokeHeight=8
render smokefarpoint Declare=Case=24 Declare=SmokeHeight=8
near "smoke far from the ball, against the point of its power" "$(mean smokefar 20 43 20 43)" \
    "$(awk -v a="$(mean smokefarpoint 20 43 20 43)" 'BEGIN { printf "%.5f", a * 1.0 }')" 0.08
echo "media_light: ok"
