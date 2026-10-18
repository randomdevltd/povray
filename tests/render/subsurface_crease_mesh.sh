#!/bin/sh
# subsurface_crease_mesh.sh <povray> <srcdir>: a hard-edged closed mesh unfolds the cloud's points across its creases where the
# same solid built from boxes does, so its inside probes do not run through its edges.
set -e
trap 'rm -f sscrease_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_crease_mesh.pov" -d -p -a +fp16 +w160 +h120 +wt2 +o"sscrease_$name.ppm" "$@" > "sscrease_$name.log" 2>&1
    tr -d ' \r\n' < "sscrease_$name.log" > "sscrease_$name.txt"
}
fail() { echo "subsurface_crease_mesh: $*"; exit 1; }
stat() { sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried,\([0-9]*\)served.*withpointsunfoldedacrosscreases\([0-9]*\);.*/\1 \2 \3/p' "sscrease_$1.txt"; }
for s in 0:L 1:cube; do
    shape=${s%:*}; solid=${s#*:}
    render ${solid}mesh Declare=Shape=$shape Declare=Mesh=1
    render ${solid}box Declare=Shape=$shape Declare=Mesh=0
    set -- $(stat ${solid}mesh)
    [ "$#" -eq 3 ] && [ "$1" -gt 0 ] && [ "$1" -eq "$2" ] || fail "the $solid mesh was not served by the cloud: $(stat ${solid}mesh)"
    [ "$solid" = cube ] || [ "$3" -gt 100 ] || fail "the L mesh unfolded too few points across its crease: $3"
    [ "$(stat ${solid}mesh)" = "$(stat ${solid}box)" ] ||
        fail "the $solid mesh unfolded points across creases unlike its boxes: mesh $(stat ${solid}mesh), boxes $(stat ${solid}box)"
done
echo "subsurface_crease_mesh: a hard-edged mesh unfolds points across its creases as the same solid of boxes does"
