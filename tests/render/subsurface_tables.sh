#!/bin/sh
# subsurface_tables.sh <povray> <srcdir>: method 2's tables stay within their cap, and dropping them changes no pixel.
set -e
trap 'rm -f sstab_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_tables.pov" -d -p -a +fp16 +w80 +h60 +o"sstab_$name.ppm" "$@" > "sstab_$name.log" 2>&1
    tr -d ' \r\n' < "sstab_$name.log" > "sstab_$name.txt"
    tail -c 14400 "sstab_$name.ppm" > "sstab_$name.px"
}
fail() { echo "subsurface_tables: $*"; exit 1; }
tables() { sed -n 's/.*Method2tables:\([0-9]*\)boundariesand\([0-9]*\)layertablesbuilt,\([0-9]*\)droppedforthe\([0-9.]*\)MBcap,peak\([0-9.]*\)MBalive.*/\1 \2 \3 \4 \5/p' "sstab_$1.txt"; }

render one +wt1
render two +wt2
for run in one two; do
    set -- $(tables $run)
    [ "$#" -eq 5 ] || fail "no table statistics on $run thread(s)"
    [ "$3" -gt 0 ] || fail "the tables never passed their cap on $run thread(s): $(tables $run)"
    # Past the cap, only the tables each thread is using may stay alive: 36 layer tables and two boundaries a thread.
    awk -v peak="$5" -v cap="$4" 'BEGIN { exit !(peak <= cap + 1.5) }' || fail "the tables outgrew their cap on $run thread(s): $(tables $run)"
done
cmp -s sstab_one.px sstab_two.px || fail "one thread and two rendered different images"
echo "subsurface_tables: method 2's tables stay within their cap, alike on any thread count"
