#!/bin/sh
# progressive_radiosity.sh <povray> <srcdir>: radiosity carries a method 2 caustic to a wall that collects no photons.
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_radiosity.pov" +w64 +h48 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_radiosity_$name.ppm" "$@" > "progressive_radiosity_$name.log" 2>&1 ||
        { cat "progressive_radiosity_$name.log"; exit 1; }
    tail -c 18432 "progressive_radiosity_$name.ppm" > "progressive_radiosity_$name.px"
}
render one +wt1
render four +wt4
cmp progressive_radiosity_one.px progressive_radiosity_four.px
render direct Declare=Radiosity=0 +wt2
render dark Declare=Caustic=0 +wt2
render classic Declare=Method=1 +wt2
: > progressive_radiosity_progressive.pov-state
ln -f progressive_radiosity_progressive.pov-state progressive_radiosity_saved.pov-state
render progressive +pr +wt2
state_bytes=$(wc -c < progressive_radiosity_saved.pov-state)
head -c $((state_bytes - 100)) progressive_radiosity_saved.pov-state > progressive_radiosity_resumed.pov-state
render resumed +pr +wt4 +c
cmp progressive_radiosity_progressive.px progressive_radiosity_resumed.px
. "$SRCDIR/tests/render/image.sh"
image_awk '
function rows(name, first, last,    v, n, i, sum) {
    n = read_words("progressive_radiosity_" name ".px", v, 0)
    sum = 0
    for (i = first * 192; i < last * 192 && i < n; i++) sum += v[i]
    return sum / ((last - first) * 192 * 65535.0)
}
BEGIN {
    one = rows("one", 24, 36); direct = rows("direct", 24, 36); dark = rows("dark", 24, 36); classic = rows("classic", 24, 36)
    printf "progressive_radiosity lower wall: %.4f radiosity, %.4f without, %.4f classic\n", one, direct, classic
    if (!(direct == 0.0 && dark == 0.0)) fail("the wall is lit without radiosity or without the caustic: " direct ", " dark)
    if (!(one > 0.02)) fail("radiosity does not carry the caustic to the wall: " one)
    if (!(0.8 < one / classic && one / classic < 1.25)) fail("the bounced caustic differs from classic photons: " one " / " classic)
    floor_one = rows("one", 36, 48); floor_direct = rows("direct", 36, 48); floor_classic = rows("classic", 36, 48)
    if (!(0.8 < floor_one / floor_classic && floor_one / floor_classic < 1.25 && floor_direct > 0.05))
        fail("floor: radiosity " floor_one ", classic " floor_classic ", direct " floor_direct)
}'
rm -f progressive_radiosity_*.ppm progressive_radiosity_*.px progressive_radiosity_*.log progressive_radiosity_*.pov-state
echo 'progressive_radiosity: bounced caustic, threads, continue and classic agreement passed'
