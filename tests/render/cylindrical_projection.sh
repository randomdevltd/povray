#!/bin/sh
# cylindrical_projection.sh <povray> <srcdir>: profiles differ, and progressive Mercator matches block order.
set -e
POVRAY=$1; SRCDIR=$2; SCENE=$SRCDIR/tools/bench/cylindrical-projections.pov
render() {
    name=$1; profile=$2; shift 2
    "$POVRAY" +i"$SCENE" +L"$SRCDIR/distribution/include" +w160 +h80 -a -d -p -v -gp +fp16 +wt1 +o"cylindrical_$name.ppm" "Declare=Profile=$profile" "$@"
    tail -c 76800 "cylindrical_$name.ppm" > "cylindrical_$name.px"
}
render mercator 1
render progressive 1 +pr
render miller 2 +pr
cmp cylindrical_mercator.px cylindrical_progressive.px
if cmp -s cylindrical_mercator.px cylindrical_miller.px; then
    echo "cylindrical projection profiles produced the same pixels"; exit 1
fi
rm -f cylindrical_*.ppm cylindrical_*.px cylindrical_*.pov-state*
echo "cylindrical projections: profiles differ, and progressive Mercator matches block order"
