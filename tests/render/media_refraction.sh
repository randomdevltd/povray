#!/bin/sh
# media_refraction.sh <povray> <srcdir>: media refraction, curved rays, and surface indices where solids meet or overlap.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=96; H=72
render() {
    name=$1; shift; version=4.0
    case $1 in Version=*) version=${1#Version=}; shift;; esac
    # 3.7 references take 4.0's default air ior, so they compare with 4.0 renders.
    if [ "$version" = 3.7 ]; then set -- Declare=Atmosphere=1.00029 "$@"; fi
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

# In vacuum a constant refraction 0.5 over the void is a uniform ior 1.5.
render constant Declare=Case=1 Declare=Atmosphere=1
render uniform Declare=Case=2 Declare=Atmosphere=1
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
# An ior 1 box around glass is vacuum and means with it, warning; without an ior, or with ior off, it takes no part.
render air_box Declare=Case=17
render air_box_37 Version=3.7 Declare=Case=17 Declare=BallIor=1.25
region air_box air_box_37 0 95 0 71
says "of ior 1 overlaps" air_box || { echo "media_refraction: no warning for an ior 1 box around glass" >&2; exit 1; }
render fog_box Declare=Case=50 Declare=Fog=1
render fog_off Declare=Case=50 Declare=Fog=1 Declare=Off=1
same fog_box fog_off
render fog_air Declare=Case=17 Declare=Fog=1
differs fog_air fog_box
render bubble_off Declare=Case=18 Declare=InnerOff=1
render glass Declare=Case=53
same bubble_off glass
# Overlapping solids of other iors mean: glass 1.5 with 1.25 is 3.7's 1.375.
for case in 18:bubble 19:inside_glass; do
    render ${case#*:} Declare=Case=${case%:*} Declare=Inner=1.25
    render ${case#*:}_37 Version=3.7 Declare=Case=${case%:*} Declare=Inner=1.375
    region ${case#*:} ${case#*:}_37 0 95 0 71
done
render bubble_cut Declare=Case=51
render bubble_air_37 Version=3.7 Declare=Case=18 Declare=Inner=1.00029
render bubble_filled Declare=Case=52
region bubble_cut bubble_air_37 0 95 0 71
# A surface or replace sphere inside mean glass gives its own ior, as a difference bubble does.
for mix in 0:surface 2:replace; do
    render bubble_${mix#*:} Declare=Case=18 Declare=Mix=${mix%:*} Declare=InnerAir=1
    region bubble_${mix#*:} bubble_cut 0 95 0 71
    render filled_${mix#*:} Declare=Case=18 Declare=Mix=${mix%:*} Declare=Inner=1.25
    region filled_${mix#*:} bubble_filled 0 95 0 71
done
render bubble_replace_133 Declare=Case=18 Declare=Mix=2 Declare=InnerAir=1 Declare=Atmosphere=1.33
render bubble_cut_133 Declare=Case=51 Declare=Atmosphere=1.33
region bubble_replace_133 bubble_cut_133 0 95 0 71
# A replace placed before its glass means with it, unless its priority outranks the glass; in vacuum the mean is 1.25.
render replace_first Declare=Case=54 Declare=Atmosphere=1
render bubble_filled_vacuum Declare=Case=52 Declare=Atmosphere=1
region replace_first bubble_filled_vacuum 0 95 0 71
render replace_priority Declare=Case=54 Declare=Priority=1
region replace_priority bubble_cut 0 95 0 71
render lens_bubble Declare=Case=55
render lens_bubble_cut Declare=Case=56
region lens_bubble lens_bubble_cut 0 95 0 71
# The void is the atmosphere's ior: a ball under atmospheric_ior 1.33 is the ball in water, and ior 1.33 is void there.
render underwater Declare=Case=57 Declare=Atmosphere=1.33
render in_water Declare=Case=58
region underwater in_water 14 81 2 69
render underwater_off Declare=Case=59 Declare=Atmosphere=1.33
same underwater underwater_off
# An ior off box of refracting media bends from the index around it, water or atmosphere alike, with no surface of its own.
render lens_water Declare=Case=66
render lens_air_133 Declare=Case=67 Declare=Atmosphere=1.33
render water_only Declare=Case=68
region lens_water lens_air_133 14 81 2 69
differs lens_water water_only
region lens_water water_only 67 70 13 16
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
# A shared clear face blends both textures, the same from either side; near and far show one box's, by the side seen from.
render blend_left Declare=Case=62
render blend_right Declare=Case=62 Declare=View=1
same blend_left blend_right
expect blend_left 48 36 0.28
render blend_apart Declare=Case=63
region blend_left blend_apart 0 95 0 71
for side in 1:near:0.4:0.16 2:far:0.16:0.4; do
    mode=$(echo $side | cut -d: -f2)
    render ${mode}_left Declare=Case=62 Declare=Side=${side%%:*}
    render ${mode}_right Declare=Case=62 Declare=Side=${side%%:*} Declare=View=1
    expect ${mode}_left 48 36 $(echo $side | cut -d: -f3)
    expect ${mode}_right 48 36 ${side##*:}
done
# A replace object owns its faces where they meet what it replaces, whatever interface_texture says.
for side in 1 2 3; do
    render replace_slab_$side Declare=Case=64 Declare=Side=$side
    render replace_hole_$side Declare=Case=65 Declare=Side=$side
    region replace_slab_$side replace_hole_$side 0 95 0 71
done
# Under 4.0 a scene whose clear objects are all ior_mix surface renders as 3.7 does.
render hidden_surface Declare=Case=11 Declare=MixAll=0
same hidden_surface hidden_37
render bubble_surface Declare=Case=18 Declare=MixAll=0 Declare=Inner=1.00029
same bubble_surface bubble_air_37

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
