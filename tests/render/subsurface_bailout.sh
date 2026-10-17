#!/bin/sh
# subsurface_bailout.sh <povray> <srcdir>: hits below the subsurface bailout build no cloud, alike on any thread count.
set -e
trap 'rm -f ssbail_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_bailout.pov" -d -p -a +fp16 +w64 +h48 +o"ssbail_$name.ppm" "$@" > "ssbail_$name.log" 2>&1
    tr -d ' \r\n' < "ssbail_$name.log" > "ssbail_$name.txt"
    tail -c 18432 "ssbail_$name.ppm" > "ssbail_$name.px"
}
fail() { echo "subsurface_bailout: $*"; exit 1; }
tried() { sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried.*/\1/p' "ssbail_$1.txt"; }
estimated() { sed -n 's/.*;\([0-9]*\)shadingpointsbelowthesubsurfacebailout.*/\1/p' "ssbail_$1.txt"; }
cells() { sed -n 's/.*MBwithrowsbeingcast;\([0-9]*\)cells,\([0-9]*\)pointsbuilt.*/\1 \2/p' "ssbail_$1.txt"; }
same() { cmp -s "ssbail_$1.px" "ssbail_$2.px"; }

render faint +wt2
[ "$(tried faint)" = 0 ] && [ "$(estimated faint)" -gt 0 ] && [ "$(cells faint)" = "0 0" ] ||
    fail "hits in a faint mirror reached the cloud: tried $(tried faint), estimated $(estimated faint), cells $(cells faint)"
render off +wt2 Declare=Bailout=0
[ "$(tried off)" -gt 0 ] && [ "$(estimated off)" = 0 ] && [ "$(cells off | cut -d' ' -f1)" -gt 0 ] ||
    fail "bailout 0 did not build the cloud: tried $(tried off), estimated $(estimated off), cells $(cells off)"
same faint off && fail "the estimate drew the faint mirror exactly as the cloud does"

render one +wt1 Declare=Direct=1
render four +wt4 Declare=Direct=1
[ "$(estimated one)" -gt 0 ] && [ "$(tried one)" -gt 0 ] || fail "the direct view did not mix cloud and estimated hits"
same one four || fail "one thread and four rendered different images"
[ "$(cells one)" = "$(cells four)" ] && [ "$(estimated one)" = "$(estimated four)" ] || fail "one thread and four built different clouds"
echo "subsurface_bailout: hits below the bailout build no cloud, alike on any thread count"
