#!/bin/sh
# progressive_volume_groups.sh <povray> <srcdir>: media photons follow light-group membership and global_lights off.
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_volume_groups.pov" +w80 +h40 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_volume_groups_$name.ppm" "$@" > "progressive_volume_groups_$name.log" 2>&1 ||
        { cat "progressive_volume_groups_$name.log"; exit 1; }
    tail -c 19200 "progressive_volume_groups_$name.ppm" > "progressive_volume_groups_$name.px"
}
render one +wt1
render four +wt4
cmp progressive_volume_groups_one.px progressive_volume_groups_four.px
render left Declare=Right=0 +wt2
render right Declare=Left=0 +wt2
render group_lamp Declare=RightLamp=0 +wt2
render global_lamp Declare=LeftLamp=0 +wt2
render open_global Declare=Grouped=0 Declare=LeftLamp=0 +wt2
render open_group Declare=Grouped=0 Declare=RightLamp=0 +wt2
. "$SRCDIR/tests/render/image.sh"
image_awk '
function half(name, side, out,    v, n, i, k) {
    n = read_words("progressive_volume_groups_" name ".px", v, 0)
    k = 0
    for (i = 0; i < n; i++) if ((int(i / 3) % 80 >= 40) == (side == "right")) out[k++] = v[i]
    return k
}
function mean(name, side,    v, n) {
    n = half(name, side, v)
    return sum_of(v, n) / (n * 65535.0)
}
function close_halves(a, b,    first, second, n, i) {
    n = half(a[1], a[2], first)
    half(b[1], b[2], second)
    for (i = 0; i < n; i++) if (abs(first[i] - second[i]) > 3) return 0
    return 1
}
BEGIN {
    if (max_of(v, half("group_lamp", "right", v)) != 0) fail("a group light lit media outside its group")
    if (max_of(v, half("global_lamp", "left", v)) != 0) fail("a global light lit media in a global_lights off group")
    if (!(mean("open_global", "left") > 0.00025 && mean("open_group", "right") > 0.00025))
        fail("the lamps do not reach the other box: " mean("open_global", "left") ", " mean("open_group", "right"))
    split("group_lamp left", a, " "); split("left left", b, " ")
    if (!close_halves(a, b)) fail("the group box differs from the group alone")
    split("global_lamp right", a, " "); split("right right", b, " ")
    if (!close_halves(a, b)) fail("the global box differs from the global half alone")
    for (s = 0; s < 2; s++) {
        side = s ? "right" : "left"
        together = mean("one", side); alone = mean(side, side)
        if (!(alone > 0.0025 && abs(together / alone - 1) < 0.02))
            fail(side " box changes when both lamps shoot: " together " together, " alone " alone")
    }
}'
rm -f progressive_volume_groups_*.ppm progressive_volume_groups_*.px progressive_volume_groups_*.log
echo 'progressive_volume_groups: group and global media lighting, isolation, controls and threads passed'
