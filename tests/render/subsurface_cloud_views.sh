#!/bin/sh
# subsurface_cloud_views.sh <povray> <srcdir>: two cameras of one job share their subsurface method 2 clouds, and each camera's
# image matches a render of it alone pixel for pixel, with a translucent plane each camera spaces by its own pixel, with
# radiosity and with photons, whichever camera comes first and on any thread count; the second camera reuses the box's cells.
set -eu
trap 'rm -f ssviews_*' EXIT
POVRAY=$1; SRCDIR=$2
scene=$SRCDIR/tests/render/subsurface_cloud_views.pov
fail() { echo "subsurface_cloud_views: $*" >&2; exit 1; }
run() {
    name=$1; shift
    rm -f "ssviews_$name.ppm" "ssviews_$name"-[0-9]*.ppm
    "$POVRAY" +i"$scene" +w64 +h48 -a -d -p -v -gp +fp16 File_Gamma=1 +o"ssviews_$name.ppm" "$@" > "ssviews_$name.log" 2>&1 || {
        cat "ssviews_$name.log"; fail "$name did not render"; }
}
same() {
    tail -c 18432 "ssviews_$1.ppm" > "ssviews_$1.px"
    tail -c 18432 "ssviews_$2.ppm" > "ssviews_$2.px"
    cmp -s "ssviews_$1.px" "ssviews_$2.px" || fail "$1 differs from $2"
}
# the cells of the second view's clouds that the first built
reused() {
    tr -d ' \r\n' < "ssviews_$1.log" | sed -n "s/.*Method2pointcloudsharedbythisscene'sviews:\([0-9]*\)ofthe\([0-9]*\)cells.*/\1 \2/p"
}
# each view's cloud digest, in render order
digests() {
    tr -d ' \r\n' < "ssviews_$1.log" | grep -o 'digest[0-9a-f]\{16\}\.' | tr -d '.'
}

for case in 0 1 2; do
    run "c$case" Declare=Case=$case +wt2
    run "c${case}_alone0" Declare=Case=$case Declare=Cam=0 +wt2
    run "c${case}_alone1" Declare=Case=$case Declare=Cam=1 +wt2
    same "c$case-1" "c${case}_alone0"
    same "c$case-2" "c${case}_alone1"
done

# the box's cells were built once and served the second camera; the plane's were built for each
set -- $(reused c0)
[ "${1:-0}" -gt 0 ] || fail "the second camera reused no cells of the first"
[ "$1" -lt "$2" ] || fail "the second camera reused every cell, the plane's spaced for the first camera among them"
# each view reports the clouds it used, as a render of its camera alone does
[ "$(digests c0)" = "$(digests c0_alone0; digests c0_alone1)" ] || fail "a view's cloud digest differs from its camera's alone"

# whichever camera comes first, and on any thread count
run order Declare=Order=1 +wt2
same order-1 c0_alone1
same order-2 c0_alone0
run threads Declare=Case=0 +wt5
same threads-1 c0_alone0
same threads-2 c0_alone1
echo "subsurface_cloud_views passed"
