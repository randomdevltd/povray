#!/bin/sh
# snapshot.sh <povray> <srcdir>: a snapshot from the state file alone matches the render's own, and snapshots leave the render unchanged.
set -e
POVRAY=$1; SRCDIR=$2
W=96; H=64
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/snapshot.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp -a +wt1 +bs16 +fp16 +o"snapshot_$name.ppm" "$@"
}
pixels() {
    # the header carries the render date, so only the pixels of 6 bytes are compared
    tail -c $((W * H * 6)) "snapshot_$1.ppm" > "snapshot_$1.px"
}
offline() {
    "$POVRAY" +w$W +h$H -d -p -v -gp Snapshot_From="$1" +SN"$2"
}
for mode in -pr +pr; do
    # a second link keeps the whole render's state after the render deletes it
    rm -f snapshot_*.ppm snapshot_*.px snapshot_*.png snapshot_*.pov-state*
    : > snapshot_whole.pov-state
    ln snapshot_whole.pov-state snapshot_kept.pov-state
    render whole $mode +SNsnapshot_whole.png
    before=$(cksum < snapshot_kept.pov-state)
    offline snapshot_kept.pov-state snapshot_kept.png
    if [ "$(cksum < snapshot_kept.pov-state)" != "$before" ]; then
        echo "snapshot $mode: writing a snapshot changed the state file"; exit 1
    fi
    cmp snapshot_whole.png snapshot_kept.png
    cmp snapshot_whole.png.heat.png snapshot_kept.png.heat.png
    grep -q '^timed_block_records=[1-9]' snapshot_kept.png.heat.txt
    # cut at record boundaries, each record starting with its POVMS header, so the cut does not depend on record sizes
    records=$(grep -boa POVRAYMS snapshot_kept.pov-state | cut -d: -f1)
    count=$(echo "$records" | wc -l)
    if [ "$count" -lt 4 ]; then
        echo "snapshot $mode: the state file holds only $count records"; exit 1
    fi
    head -c $(echo "$records" | sed -n 2p) snapshot_kept.pov-state > snapshot_first.pov-state
    offline snapshot_first.pov-state snapshot_first.png
    if cmp -s snapshot_first.png snapshot_whole.png; then
        echo "snapshot $mode: the first of $count records gave the whole image"; exit 1
    fi
    head -c $(echo "$records" | sed -n $((count / 2 + 1))p) snapshot_kept.pov-state > snapshot_resumed.pov-state
    render resumed $mode +c +SNsnapshot_resumed.png
    pixels whole; pixels resumed
    cmp snapshot_whole.px snapshot_resumed.px
    cmp snapshot_whole.png snapshot_resumed.png
    test "$(sed -n 's/^timed_block_records=//p' snapshot_whole.png.heat.txt)" = \
         "$(sed -n 's/^timed_block_records=//p' snapshot_resumed.png.heat.txt)"
    if "$POVRAY" +w$((W - 1)) +h$H -d -p -v -gp Snapshot_From=snapshot_kept.pov-state +SNsnapshot_bad.png 2> /dev/null; then
        echo "snapshot $mode: a state file wider than +W should be refused"; exit 1
    fi
done
rm -f snapshot_*.ppm snapshot_*.px snapshot_*.png snapshot_*.pov-state*
echo "snapshot: a state file's snapshot matches the render's, and a continued render is unchanged"
