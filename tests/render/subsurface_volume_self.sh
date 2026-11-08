#!/bin/sh
# subsurface_volume_self.sh <povray> <srcdir>: a volume light inside its own subsurface host lights the host with its
# emission plus the self share of its bounce: self 0 looks as without bounce, self 1 adds the bounce, a half share
# adds half of it.
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
render none
render off Declare=Bounce=2 Declare=Self=0
render on Declare=Bounce=1
render quarter Declare=Bounce=1 Declare=Self=0.25
render half Declare=Bounce=1 Declare=Self=0.5
render most Declare=Bounce=1 Declare=Self=0.75
# Shares strictly between 0 and 1 draw alike, so the image is linear in the share up to clipped pixels; the rest differ
# by noise only.
echo "$(sum none) $(sum off) $(sum on) $(sum quarter) $(sum half) $(sum most)" | awk '
    function abs(v) { return v < 0 ? -v : v }
    { if (!($1 > 0)) { print "subsurface_volume_self: the host is dark without bounce"; exit 1 }
      if (abs($2 - $1) > 0.03 * $1) { print "subsurface_volume_self: self 0 differs from no bounce: " $2 " vs " $1; exit 1 }
      if (!($3 > 1.06 * $1)) { print "subsurface_volume_self: the bounce does not brighten its host: " $3 " vs " $1; exit 1 }
      if (!($6 > $5 && $5 > $4) || abs($5 - ($4 + $6) / 2) > 0.015 * $5) {
          print "subsurface_volume_self: the host does not take the self share of the bounce: " $4 " " $5 " " $6; exit 1 } }'
