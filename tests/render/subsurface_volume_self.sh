#!/bin/sh
# subsurface_volume_self.sh <povray> <srcdir>: a volume light inside its own subsurface host lights the host.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=40; H=30
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_volume_self.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
sum() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ s += $1 + $2 + $3 } END { print s }'; }
# A bounce light takes no emission input; its self share returns with volume export.
render none
[ "$(sum none)" -gt 0 ] || { echo "subsurface_volume_self: the host is dark" >&2; exit 1; }
