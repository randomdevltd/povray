#!/bin/sh
# subsurface_minimum_influence.sh <povray> <srcdir>: hits below minimum_influence build no cloud, alike on any thread count.
set -e
trap 'rm -f ssmin_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_minimum_influence.pov" -d -p -a +fp16 +w64 +h48 +o"ssmin_$name.ppm" "$@" > "ssmin_$name.log" 2>&1
    tr -d ' \r\n' < "ssmin_$name.log" > "ssmin_$name.txt"
    tail -c 18432 "ssmin_$name.ppm" > "ssmin_$name.px"
}
fail() { echo "subsurface_minimum_influence: $*"; exit 1; }
tried() { sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried.*/\1/p' "ssmin_$1.txt"; }
estimated() { sed -n 's/.*;\([0-9]*\)shadingpointsbelowtheirminimum_influence.*/\1/p' "ssmin_$1.txt"; }
cells() { sed -n 's/.*MBwithrowsbeingcast;\([0-9]*\)cells,\([0-9]*\)pointsbuilt.*/\1 \2/p' "ssmin_$1.txt"; }
counts() { echo "$(tried "$1") $(estimated "$1") $(cells "$1")"; }
same() { cmp -s "ssmin_$1.px" "ssmin_$2.px"; }

render faint +wt2
[ "$(tried faint)" = 0 ] && [ "$(estimated faint)" -gt 0 ] && [ "$(cells faint)" = "0 0" ] ||
    fail "hits in a faint mirror reached the cloud: tried $(tried faint), estimated $(estimated faint), cells $(cells faint)"
render off +wt2 Declare=Default=0
[ "$(tried off)" -gt 0 ] && [ "$(estimated off)" = 0 ] && [ "$(cells off | cut -d' ' -f1)" -gt 0 ] ||
    fail "a #default minimum_influence of 0 did not build the cloud: tried $(tried off), estimated $(estimated off), cells $(cells off)"
same faint off && fail "the estimate drew the faint mirror exactly as the cloud does"
render own +wt2 Declare=Default=0 Declare=Influence=0.05
same faint own && [ "$(counts faint)" = "$(counts own)" ] || fail "the wax finish's own minimum_influence did not override the #default one"
render alone +wt2 Declare=Default=0.002
same faint alone && [ "$(counts faint)" = "$(counts alone)" ] || fail "a #default giving only minimum_influence changed another finish"

render one +wt1 Declare=Direct=1
render four +wt4 Declare=Direct=1
[ "$(estimated one)" -gt 0 ] && [ "$(tried one)" -gt 0 ] || fail "the direct view did not mix cloud and estimated hits"
same one four || fail "one thread and four rendered different images"
[ "$(cells one)" = "$(cells four)" ] && [ "$(estimated one)" = "$(estimated four)" ] || fail "one thread and four built different clouds"
echo "subsurface_minimum_influence: hits below minimum_influence build no cloud, alike on any thread count"
