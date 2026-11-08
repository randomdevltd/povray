#!/bin/sh
# near_hit_other_object.sh <povray> <srcdir>: a reflected or refracted ray hits an opaque object however near it starts.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
N=16; failed=0
for c in 0 1; do
    "$POVRAY" +i"$SRCDIR/tests/render/near_hit_other_object.pov" +w$N +h$N -a -d -p -v -gp +wt1 +fp File_Gamma=1 \
        Declare=Case=$c +o"near_hit_$c.ppm" > "near_hit_$c.log" 2>&1 || { cat "near_hit_$c.log"; exit 1; }
    tail -c $((N * N * 3)) "near_hit_$c.ppm" | od -An -v -tu1 -w3 | awk -v c=$c '
        $2 < 200 || $1 > 20 || $3 > 20 { bad++ }
        END { if (bad) { printf "near_hit_other_object case %d: %d of %d pixels miss the floor\n", c, bad, NR > "/dev/stderr"; exit 1 } }' || failed=1
done
for o in 1e-8 1e-10; do
    "$POVRAY" +i"$SRCDIR/tests/render/near_hit_other_object.pov" +w$N +h$N -a -d -p -v -gp +wt1 +fp File_Gamma=1 \
        Declare=Case=2 Declare=Overlap=$o +o"overlap_$o.ppm" > "overlap_$o.log" 2>&1 || { cat "overlap_$o.log"; exit 1; }
    tail -c $((N * N * 3)) "overlap_$o.ppm" > "overlap_$o.rgb"
done
cmp -s overlap_1e-8.rgb overlap_1e-10.rgb || { echo "near_hit_other_object: liquid overlapping its glass by 1e-8 and 1e-10 renders differently" >&2; failed=1; }
[ $failed -eq 0 ] || exit 1
echo "near_hit_other_object: reflected and refracted rays find the floor 2e-5 to 1e-4 beyond the surface they leave; a clear one that near is the same interface"
