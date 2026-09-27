#!/bin/sh
# same_image.sh <povray> <srcdir>: random effects must render the same with one thread or four, and in another block order.
set -e
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/random_effects.pov" +L"$SRCDIR/include" +w96 +h72 -a -d -p -v -gp +fp16 +o"same_image_$name.ppm" "$@"
    # the header carries the render date, so only the 96x72 pixels of 6 bytes are compared
    tail -c 41472 "same_image_$name.ppm" > "same_image_$name.px"
}
POVRAY=$1; SRCDIR=$2
render wt1 +wt1
render wt4 +wt4
render order +wt4 +rp5 +bs5
cmp same_image_wt1.px same_image_wt4.px
cmp same_image_wt1.px same_image_order.px
rm -f same_image_*.ppm same_image_*.px
echo "same_image: random effects render the same across threads and block order"
