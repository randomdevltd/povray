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
# slab <sigma> <depth>: what a white probe on the 128-wide emitting slab reads, (j/sigma) times
# 2*integral over mu of mu*(1-exp(-tau/mu)), with j = L/(4V/A) and the slab's own absorption included.
slab() {
    awk -v sigma="$1" -v depth="$2" 'BEGIN {
        tau = sigma * depth; s = 0; n = 20000; reference = 4 * 128 * 128 * depth / (2 * 128 * 128 + 4 * 128 * depth)
        for (i = 1; i <= n; i++) { mu = (i - 0.5) / n; e = -tau / mu; s += mu * ((e < -700) ? 1 : 1 - exp(e)) }
        printf "%.5f", 2 * s / n / (sigma * reference) }'
}

# 1. the probe: a white plane on an emitting slab of optical depth 2 reads the slab's escape radiance,
#    and a slab of a quarter the density over four times the depth reads its own, from its own reference length
render probe Declare=Case=1
near "the probe on the slab" "$(mean probe 24 39 24 39)" "$(slab 1 2)" 0.02
render probe_sigma Declare=Case=1 Declare=Sigma=0.25 Declare=Depth=8
near "the probe on the wide faint slab" "$(mean probe_sigma 24 39 24 39)" "$(slab 0.25 8)" 0.02

# 2. the scaled room: radiance anchored, so half the scene reads the same escape radiance. The
#    draws key on the ray's position, so the two images are not the same bytes; the reading is the same.
render room Declare=Case=1
render room_half Declare=Case=2
near "the room at full scale" "$(mean room 24 39 24 39)" "$(slab 1 2)" 0.02
near "the room at half scale" "$(mean room_half 24 39 24 39)" "$(slab 1 2)" 0.02

# 3. the bulb: a glowing gas ball lights the floor like an area light of the same radiance over the
#    same silhouette, and the point under it reads the radiance times the silhouette over the height
render bulb Declare=Case=3
render bulb_area Declare=Case=3 Declare=Area=1
near "the floor under the ball" "$(mean bulb 30 33 30 33)" 0.25 0.04
for block in "8 23 8 23" "40 55 8 23" "24 39 24 39"; do
    near "the bulb against its area light, block $block" "$(mean bulb $block)" "$(mean bulb_area $block)" 0.05
done

# 3b. the container's shape: a lathe and a sor bounding the same cylinder light the floor as the cylinder does
render shape_cylinder Declare=Case=3 Declare=Shape=1
render shape_lathe Declare=Case=3 Declare=Shape=2
render shape_sor Declare=Case=3 Declare=Shape=3
for block in "8 23 8 23" "24 39 24 39"; do
    for shape in lathe sor; do
        near "the $shape against the cylinder, block $block" "$(mean shape_$shape $block)" "$(mean shape_cylinder $block)" 0.03
    done
done

# 4. the grid: the emitter's resolution changes its cost, never its power; the absorption-1 ball with
#    j = L/(4R/3) reads (j/sigma)*[R^2-(1-(1+2*sigma*R)*exp(-2*sigma*R))/(2*sigma^2)]/height^2
render grid_fine Declare=Case=4 Declare=Res=0.06
render grid_coarse Declare=Case=4 Declare=Res=0.25
for block in "30 33 30 33" "24 39 24 39" "8 23 8 23"; do
    near "the grid's resolution, block $block" "$(mean grid_fine $block)" "$(mean grid_coarse $block)" 0.06
done
near "the fine grid against the analytic point" "$(mean grid_fine 30 33 30 33)" \
    "$(awk 'BEGIN { s = 1; R = 0.25; printf "%.5f", 16 * 3 / (4 * s * R) * (R * R - (1 - (1 + 2 * s * R) * exp(-2 * s * R)) / (2 * s * s)) / 4 }')" 0.06

# 5. the pivot: within the emitter's reach every attenuation scale leaves the probe reading untouched;
#    draws from the slab's far corners lie just past its bounding radius, so the readings agree, not the bytes
render pivot_one Declare=Case=5 Declare=Att=1
render pivot_faint Declare=Case=5 Declare=Att=8
render pivot_bright Declare=Case=5 Declare=Att=0.125
near "the probe at attenuation scale 8" "$(mean pivot_faint 0 63 0 63)" "$(mean pivot_one 0 63 0 63)" 0.002
near "the probe at attenuation scale 1/8" "$(mean pivot_bright 0 63 0 63)" "$(mean pivot_one 0 63 0 63)" 0.002

# 6. and the point at exactly the bounding radius, the pivot itself, is left untouched too
render tangent_one Declare=Case=6 Declare=Att=1
render tangent_faint Declare=Case=6 Declare=Att=8
near "the tangent point at the pivot radius" "$(mean tangent_one 29 34 29 34)" "$(mean tangent_faint 29 34 29 34)" 0.005

# 7. samples are draws, never light: the same flame at sixteen draws and at five hundred renders the
#    same floor, its glow medium riding every shadow ray, in the near field under it and farther out
render flame_few Declare=Case=7 Declare=Samples=16
render flame_many Declare=Case=7 Declare=Samples=512
for block in "24 39 24 39" "8 23 8 23" "40 55 40 55"; do
    near "the flame's floor at 16 draws, block $block" "$(mean flame_few $block)" "$(mean flame_many $block)" 0.07
done

# 8. and the far field keeps the emitter's angular extent: the shadow edge a plate casts on a wall is
#    a gradient as wide as the emitter's angular diameter times the plate's offset, its 10-to-90 span
#    1.44 of the emitter's own width for a uniform disk, room-scale and at the sun's 215 radii
edgewidth() {
    tail -c $(( 6 * W * H )) "volume_light_$1.ppm" | od -An -v -tu1 -w6 | awk -v w=$W -v frame=$2 '
        { c = (NR - 1) % w; v = ($1 * 256 + $2) / 65535; s[c] += v; n[c]++ }
        END {
            lo = 1; hi = 0
            for (i = 0; i < w; i++) { p[i] = s[i] / n[i]; if (p[i] < lo) lo = p[i]; if (p[i] > hi) hi = p[i] }
            t10 = lo + 0.1 * (hi - lo); t90 = lo + 0.9 * (hi - lo)
            for (i = 0; i < w; i++) { if (p[i] > t10) { x10 = i; break } }
            for (i = 0; i < w; i++) { if (p[i] >= t90) { x90 = i; break } }
            printf "%.5f", (x90 - x10) * frame / w }'
}
render pen_room Declare=Case=8
near "the room penumbra against the emitter's angular width" "$(edgewidth pen_room 0.012)" \
    "$(awk 'BEGIN { printf "%.5f", 0.72 * 0.03 * 2 * 0.1 / (3 - 0.03) }')" 0.25
render pen_sun Declare=Case=9
near "the sun penumbra against the emitter's angular width" "$(edgewidth pen_sun 0.03)" \
    "$(awk 'BEGIN { printf "%.5f", 0.72 * 0.5 * 2 * 0.6957 / (149.6 - 0.5) }')" 0.25
echo "volume_light: ok"
