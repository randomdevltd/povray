#!/bin/sh
# subsurface_fallback.sh <povray> <srcdir>: the cloud serves every former fallback case, alike on one thread and two.
set -e
trap 'rm -f ssfall_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_fallback.pov" +L"$SRCDIR/distribution/include" -d -p -a +fp16 +w64 +h48 \
        +o"ssfall_$name.ppm" "$@" > "ssfall_$name.log" 2>&1
    tr -d ' \r\n' < "ssfall_$name.log" > "ssfall_$name.txt"
}
fail() { echo "subsurface_fallback: $*"; exit 1; }
stat() { sed -n "s/.*Method2pointcloud:\([0-9]*\)shadingpointstried,\([0-9]*\)served(\([0-9]*\)intheradiositypretrace),\([0-9]*\)leftwithoutsubsurfacelight.*/\1 \2 \3 \4/p" "ssfall_$1.txt"; }
count() { sed -n "s/.*$2\([0-9]*\).*/\1/p" "ssfall_$1.txt"; }
for c in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17; do
    render case$c +wt2 Declare=Case=$c
    set -- $(stat case$c)
    [ "$#" -eq 4 ] && [ "$1" -gt 0 ] || fail "case $c reported no shading points: $(stat case$c)"
    case $c in
        1|2) [ "$2" -eq 0 ] && [ "$4" -eq "$1" ] || fail "the open sheet in case $c was served: $(stat case$c)"
             grep -q 'Subsurfacemethod2left[0-9]*shadingpointsofthe\(mesh\|disc\)at.*asitisanopensheet' "ssfall_case$c.txt" ||
                 fail "no warning names the open sheet in case $c" ;;
        *) [ "$1" -eq "$2" ] && [ "$4" -eq 0 ] || fail "case $c was not served in full: $(stat case$c)"
           ! grep -q 'Subsurfacemethod2left' "ssfall_case$c.txt" || fail "case $c named an object left without subsurface light" ;;
    esac
    [ "$c" -ne 13 ] || [ "$3" -gt 0 ] || fail "the radiosity pretrace was not served by the cloud"
done
[ "$(count case6 'nearfielddrawnat')" -gt 0 ] && [ "$(count case6 'gatherswithlargercells')" -gt 0 ] ||
    fail "the wide ring drew no near field or raised no cells"
[ "$(count case4 'gatherswithlargercells')" -gt 0 ] || fail "the plane raised no cells"
for c in 5 6 7 9 10 12 13 15 16 17; do
    render one$c +wt1 Declare=Case=$c
    tail -c 18432 "ssfall_one$c.ppm" > ssfall_a.px; tail -c 18432 "ssfall_case$c.ppm" > ssfall_b.px
    cmp -s ssfall_a.px ssfall_b.px || fail "case $c differs between one thread and two"
done
echo "subsurface_fallback: every former fallback case is served by the cloud or names its open sheet, alike on any thread count"
