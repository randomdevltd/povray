#!/bin/sh
# subsurface_boundaries.sh <povray> <srcdir>: past its tables' cap method 2 drops boundaries of four indices and builds others in
# their memory; each wedge still renders alike with the others shown or hidden, on one thread or two, run after run.
set -e
trap 'rm -f ssbound_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_boundaries.pov" -d -p -a +fp16 +w240 +h240 +o"ssbound_$name.ppm" "$@" > "ssbound_$name.log" 2>&1
    tr -d ' \r\n' < "ssbound_$name.log" > "ssbound_$name.txt"
    tail -c 345600 "ssbound_$name.ppm" | od -An -v -w6 -tx1 > "ssbound_$name.px"
}
fail() { echo "subsurface_boundaries: $*"; exit 1; }
tables() { sed -n 's/.*Method2tables:\([0-9]*\)boundariesand\([0-9]*\)layertablesbuilt,\([0-9]*\)droppedforthe.*/\1 \2 \3/p' "ssbound_$1.txt"; }

render one +wt1 Declare=Only=0
render two +wt2 Declare=Only=0
render again +wt2 Declare=Only=0
set -- $(tables one)
[ "$#" -eq 3 ] || fail "no table statistics"
[ "$1" -ge 4 ] && [ "$3" -gt 0 ] || fail "the four boundaries were not built or the tables never passed their cap: $(tables one)"
cmp -s ssbound_one.px ssbound_two.px || fail "one thread and two rendered different images"
cmp -s ssbound_two.px ssbound_again.px || fail "two runs on two threads rendered different images"
for k in 1 2 3 4; do
    render only$k +wt2 Declare=Only=$k
    shown=$(paste -d'|' ssbound_only$k.px ssbound_two.px | awk -F'|' '$1 != " 00 00 00 00 00 00" { n++; if ($1 != $2) d++ } END { print n + 0, d + 0 }')
    set -- $shown
    [ "$1" -gt 200 ] || fail "wedge $k covers only $1 pixels"
    [ "$2" -eq 0 ] || fail "wedge $k rendered $2 of its $1 pixels differently with the other wedges shown"
done
echo "subsurface_boundaries: boundaries dropped past the cap leave every wedge's pixels alike, on any thread count and run"
