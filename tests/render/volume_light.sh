#!/bin/sh
# volume_light.sh <povray> <srcdir>: the radiance law of interior lights against analytic values.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=64
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_light.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 \
        +o"volume_light_$name.ppm" "$@" > "volume_light_$name.log" 2>&1 || { cat "volume_light_$name.log"; exit 1; }
}
mean() {
    tail -c $(( 6 * W * H )) "volume_light_$1.ppm" | od -An -v -tu1 -w6 | awk -v w=$W -v c0=$2 -v c1=$3 -v r0=$4 -v r1=$5 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 { for (k = 1; k <= 5; k += 2) { s += ($k * 256 + $(k + 1)) / 65535; n++ } }
        END { printf "%.5f", s / n }'
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) "volume_light_$name.ppm" > "volume_light_$name.pixels"; done
    cmp -s "volume_light_$1.pixels" "volume_light_$2.pixels" || { echo "volume_light: $1 differs from $2" >&2; exit 1; }
}
near() {
    awk -v a="$2" -v b="$3" -v t="$4" 'BEGIN { exit !(a - b <= t * b && b - a <= t * b) }' ||
        { echo "volume_light: $1 is $2, expected $3 within $4" >&2; exit 1; }
}
# slab <tau>: what a white probe resting on an emitting slab of optical depth tau reads, L times
# 2*integral over mu of mu*(1-exp(-tau/mu)), the slab's own absorption included.
slab() {
    awk -v tau="$1" 'BEGIN {
        s = 0; n = 20000
        for (i = 1; i <= n; i++) { mu = (i - 0.5) / n; e = -tau / mu; s += mu * ((e < -700) ? 1 : 1 - exp(e)) }
        printf "%.5f", 2 * s / n }'
}

# 1. the probe: a white plane on an emitting slab of optical depth 2 reads the slab's escape radiance,
#    and a slab of a quarter the density over four times the depth reads the same
render probe Declare=Case=1
near "the probe on the slab" "$(mean probe 24 39 24 39)" "$(slab 2)" 0.02
render probe_sigma Declare=Case=1 Declare=Sigma=0.25 Declare=Depth=8
near "the probe on the wide faint slab" "$(mean probe_sigma 24 39 24 39)" "$(slab 2)" 0.02

# 2. the scaled room: radiance anchored, so half the scene reads the same escape radiance. The
#    draws key on the ray's position, so the two images are not the same bytes; the reading is the same.
render room Declare=Case=1
render room_half Declare=Case=2
near "the room at full scale" "$(mean room 24 39 24 39)" "$(slab 2)" 0.02
near "the room at half scale" "$(mean room_half 24 39 24 39)" "$(slab 2)" 0.02

# 3. the bulb: a glowing gas ball lights the floor like an area light of the same radiance over the
#    same silhouette, and the point under it reads the radiance times the silhouette over the height
render bulb Declare=Case=3
render bulb_area Declare=Case=3 Declare=Area=1
near "the floor under the ball" "$(mean bulb 30 33 30 33)" 0.25 0.04
for block in "8 23 8 23" "40 55 8 23" "24 39 24 39"; do
    near "the bulb against its area light, block $block" "$(mean bulb $block)" "$(mean bulb_area $block)" 0.05
done

# 4. the grid: the emitter's resolution changes its cost, never its power; the absorption-1 ball is
#    fainter than a thin one by its self-absorption, L*[R^2-(1-(1+2*sigma*R)*exp(-2*sigma*R))/(2*sigma^2)]/height^2
render grid_fine Declare=Case=4 Declare=Res=0.06
render grid_coarse Declare=Case=4 Declare=Res=0.25
for block in "30 33 30 33" "24 39 24 39" "8 23 8 23"; do
    near "the grid's resolution, block $block" "$(mean grid_fine $block)" "$(mean grid_coarse $block)" 0.04
done
near "the fine grid against the analytic point" "$(mean grid_fine 30 33 30 33)" \
    "$(awk 'BEGIN { s = 1; R = 0.25; printf "%.5f", 16 * (R * R - (1 - (1 + 2 * s * R) * exp(-2 * s * R)) / (2 * s * s)) / 4 }')" 0.06

# 5. the pivot: within the emitter's reach every attenuation scale leaves the probe reading untouched
render pivot_one Declare=Case=5 Declare=Att=1
render pivot_faint Declare=Case=5 Declare=Att=8
render pivot_bright Declare=Case=5 Declare=Att=0.125
same pivot_one pivot_faint
same pivot_one pivot_bright

# 6. and the point at exactly the bounding radius, the pivot itself, is left untouched too
render tangent_one Declare=Case=6 Declare=Att=1
render tangent_faint Declare=Case=6 Declare=Att=8
near "the tangent point at the pivot radius" "$(mean tangent_one 29 34 29 34)" "$(mean tangent_faint 29 34 29 34)" 0.005
echo "volume_light: ok"
