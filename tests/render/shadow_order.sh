#!/bin/sh
# shadow_order.sh <povray> <srcdir>: a shadow must not depend on what the thread traced before, so a window, the thread count and block order change no pixel.
set -e
W=64; H=48; ROWS=41
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/shadow_order.pov" +w$W +h$H -d -p -v -gp +fp16 +o"shadow_order_$name.ppm" "$@"
    tail -c $((W * ROWS * 6)) "shadow_order_$name.ppm" > "shadow_order_$name.px"
}
same() {
    cmp -s "shadow_order_$1.px" "shadow_order_$2.px" || { echo "shadow_order: $1 and $2 differ"; exit 1; }
}
POVRAY=$1; SRCDIR=$2
render full -a +wt1
render window -a +wt1 +sr$((H - ROWS + 1)) +er$H
render four -a +wt4 +bs8
render order -a +wt4 +rp5 +bs5
same full window
same full four
same full order
rm -f shadow_order_*.ppm shadow_order_*.px
echo "shadow_order: shadows are the same in a window, on any thread count and in any block order"
