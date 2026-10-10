#!/bin/sh
# media_refraction.sh <povray> <srcdir>: media refraction, curved rays, and surface indices where solids meet or overlap.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=96; H=72
render() {
    name=$1; shift; version=4.0
    case $1 in Version=*) version=${1#Version=}; shift;; esac
    "$POVRAY" +i"$SRCDIR/tests/render/media_refraction.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"media_refraction_$name.ppm" Declare=Version=$version "$@" \
        > "media_refraction_$name.log" 2>&1 || { cat "media_refraction_$name.log"; exit 1; }
}
says() {
    tr -s ' \n' '  ' < "media_refraction_$2.log" | grep -q "$1"
}
# value <name> <col> <row>: red channel of a pixel, 0..1
value() {
    tail -c $(( (W * H - ($3 * W + $2)) * 6 )) "media_refraction_$1.ppm" | head -c 2 | od -An -tu1 | awk '{ printf "%.4f", ($1 * 256 + $2) / 65535 }'
}
expect() {
    got=$(value $1 $2 $3)
    awk -v a="$got" -v b="$4" -v t="${5:-0.004}" 'BEGIN { exit !(a - b <= t && b - a <= t) }' ||
        { echo "media_refraction: $1 at $2,$3 is $got, expected $4" >&2; exit 1; }
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm > media_refraction_$name.pixels; done
    cmp -s "media_refraction_$1.pixels" "media_refraction_$2.pixels" || { echo "media_refraction: $1 differs from $2" >&2; exit 1; }
}
# lit <name> <col0> <col1> <row0> <row1> <min>: every red value in the block is at least min
lit() {
    tail -c $(( 6 * W * H )) media_refraction_$1.ppm | od -An -v -tu1 -w6 | awk -v w=$W -v h=$H -v c0=$2 -v c1=$3 -v r0=$4 -v r1=$5 -v m=$6 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 && ($1 * 256 + $2) / 65535 < m { dark++ }
        END { if (NR != w * h) { print NR " of " w * h " pixels"; exit 1 } if (dark) { print dark " pixels dark"; exit 1 } }' >&2 ||
        { echo "media_refraction: $1 has dark pixels" >&2; exit 1; }
}
differs() {
    if (same $1 $2 2> /dev/null); then echo "media_refraction: $1 matches $2" >&2; exit 1; fi
}
# region <a> <b> <col0> <col1> <row0> <row1>: the two images agree within 0.004 over a block of pixels
region() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm | od -An -v -tu1 -w6 > media_refraction_$name.values; done
    paste -d' ' media_refraction_$1.values media_refraction_$2.values | awk -v w=$W -v c0=$3 -v c1=$4 -v r0=$5 -v r1=$6 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 {
            for (k = 1; k <= 5; k += 2) { d = (($k * 256 + $(k + 1)) - ($(k + 6) * 256 + $(k + 7))) / 65535; if (d > 0.004 || d < -0.004) bad++ }
        }
        END { if (bad) { print bad " channels differ"; exit 1 } }' >&2 || { echo "media_refraction: $1 differs from $2" >&2; exit 1; }
}

render constant Declare=Case=1
render uniform Declare=Case=2
region constant uniform 0 95 0 71
says "Media refraction changes by 0.5 across a surface" constant || { echo "media_refraction: no jump warning" >&2; exit 1; }

render lens Declare=Case=3
render none Declare=Case=4
says "Curved Rays" lens || { echo "media_refraction: the lens does not curve rays" >&2; exit 1; }
if says "across a surface" lens; then echo "media_refraction: a faded density warns of a jump" >&2; exit 1; fi
# Rows where the box holds no density: neither refraction nor Fresnel reflection shows.
for name in lens none; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm | head -c $(( 6 * W * 4 )) > media_refraction_${name}_top; done
cmp -s media_refraction_lens_top media_refraction_none_top || { echo "media_refraction: the faded box shows at its edge" >&2; exit 1; }
if says "Curved Rays" none; then echo "media_refraction: curved rays without media refraction" >&2; exit 1; fi

render rise Declare=Case=5 Declare=Ramp=1
render prepared Declare=Case=6 Declare=Ramp=1
render ramp Declare=Case=4 Declare=Ramp=1
expect ramp 48 36 0.4948
expect rise 48 36 0.5918 0.006
expect prepared 48 36 0.5918 0.006

for blend in 7:subtract 8:multiply 9:replace; do
    render ${blend#*:} Declare=Case=${blend%:*} Declare=Ramp=1
    region ${blend#*:} ramp 42 54 30 42
done
render outranked Declare=Case=10 Declare=Ramp=1
region outranked rise 44 52 32 40

render hidden Declare=Case=11
render water Declare=Case=15
render inside Declare=Case=12
render ball Declare=Case=13
same hidden inside
differs hidden water
render late_backdrop Declare=Case=16
render bare_box Declare=Case=50
same late_backdrop ball
same bare_box ball
# A solid that states an ior averages with the solids it overlaps: 4.0 matches 3.7 with the overlap at the mean.
render air_box Declare=Case=17
render air_box_37 Version=3.7 Declare=Case=17 Declare=BallIor=1.25
region air_box air_box_37 0 95 0 71
says "the atmosphere's, overlaps" air_box || { echo "media_refraction: no warning for an ior 1 box around glass" >&2; exit 1; }
for case in 18:bubble 19:inside_glass; do
    render ${case#*:} Declare=Case=${case%:*}
    render ${case#*:}_37 Version=3.7 Declare=Case=${case%:*} Declare=Inner=1.25
    region ${case#*:} ${case#*:}_37 0 95 0 71
done
render bubble_cut Declare=Case=51
render bubble_air_37 Version=3.7 Declare=Case=18
render bubble_filled Declare=Case=52
region bubble_cut bubble_air_37 0 95 0 71
region bubble_filled bubble 0 95 0 71
render lens_ab Declare=Case=37
render lens_ba Declare=Case=38
render lens_cut Declare=Case=39
same lens_ab lens_ba
region lens_ab lens_cut 0 95 0 71
render grazing_wall Declare=Case=40 Declare=Ramp=3
render grazing_wall_ba Declare=Case=41 Declare=Ramp=3
same grazing_wall grazing_wall_ba
expect grazing_wall 48 36 1.0
render shared_wall Declare=Case=20 Declare=Ramp=2
render shared_inside Declare=Case=21 Declare=Ramp=2
expect shared_wall 48 36 0.5915 0.006
expect shared_inside 48 36 0.5915 0.006
render wedges Declare=Case=22
render block Declare=Case=23
region wedges block 0 95 0 71
render coincident_wall Declare=Case=24
render inset_wall Declare=Case=25
region coincident_wall inset_wall 0 95 0 71
render grazing Declare=Case=26 +SC49 +EC49 +SR37 +ER37
if says "stopped after" grazing; then echo "media_refraction: a ray bending past a clipped edge is trapped" >&2; exit 1; fi
render thin_slab Declare=Case=27
region thin_slab none 0 95 0 71
render far_ball Declare=Case=28 Declare=Far=1e6
render near_ball Declare=Case=28
region far_ball near_ball 0 95 0 71
render uv_inside Declare=Case=29
for pair in 30:31:clear_wall 32:33:polygon_wall 42:43:shadow_wall; do
    render ${pair##*:} Declare=Case=${pair%%:*}
    render ${pair##*:}_inset Declare=Case=$(echo $pair | cut -d: -f2)
    region ${pair##*:} ${pair##*:}_inset 0 95 0 71
done
render bent_block Declare=Case=34
lit bent_block 36 59 24 47 0.5
render bent_glow Declare=Case=35
lit bent_glow 36 59 24 47 0.95
render hidden_37 Version=3.7 Declare=Case=11
render inside_37 Version=3.7 Declare=Case=12
region hidden_37 inside_37 0 95 0 71

# A gap inside the merge tolerance, none, or a wider one all look the same through parallel faces and filter or absorb once each.
# Columns past 78 in case 44 hold the water slab's side, which moves with the gap.
for case in 44:78:0 45:95:51 46:95:51; do
    c=${case%%:*}; c1=$(echo $case | cut -d: -f2); r0=${case##*:}
    for gap in 0 5e-5 5e-4 2e-3; do render gap${c}_$gap Declare=Case=$c Declare=Gap=$gap; done
    for gap in 5e-5 5e-4 2e-3; do region gap${c}_0 gap${c}_$gap 0 $c1 $r0 71; done
done

# Shadows through a clear union finish and agree however the union is bounded or split.
render union_split Declare=Case=47
for case in 48:union_bounded 49:union_whole; do
    render ${case#*:} Declare=Case=${case%:*}
    region union_split ${case#*:} 0 95 0 71
done

render only Declare=Case=14
if says "Media Samples" only; then echo "media_refraction: a refraction-only medium is sampled" >&2; exit 1; fi
echo "media_refraction: ok"
