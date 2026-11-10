#!/bin/sh
# subsurface_internal_varying.sh <povray> <srcdir>: a volume light buried in a host whose flesh pigment is looked up below
# the surface, or whose texture has two layers, lights it from within as it does a uniform one; without the lamp each is dark.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=40; H=30
sum() {
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_varying.pov" +w$W +h$H +wt2 +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +oout.ppm "$@" > out.log 2>&1 || { cat out.log >&2; echo "subsurface_internal_varying: $* failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) out.ppm | od -An -v -tu2 --endian=big -w6 | awk '{ s += $1 + $2 + $3 } END { print s }'
}
for material in 0 1 2; do
    lit=$(sum Declare=Material=$material); dark=$(sum Declare=Material=$material Declare=Lamp=0)
    echo "$lit $dark" | awk '{ exit !($1 > 0 && $2 == 0) }' ||
        { echo "subsurface_internal_varying: material $material lit $lit, without its lamp $dark" >&2; exit 1; }
done
echo "subsurface_internal_varying: varying flesh and layered textures are lit from within"
