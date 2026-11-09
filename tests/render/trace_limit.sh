#!/bin/sh
# trace_limit.sh <povray> <srcdir>: from #version 4.0 a ray at max_trace_level goes straight through clear slabs, not black.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/trace_limit.pov" +w16 +h12 +wt2 -a -d -p -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" \
        >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 1152 "$name.ppm" >"$name.rgb"
}
mean() { od -An -v -tu1 "$1.rgb" | awk '{ for (i = 1; i <= NF; i++) if (n++ % 2) s += hi * 256 + $i; else hi = $i } END { print s / (n / 2) / 65535 }'; }
# the largest difference of one 16-bit channel between two images, as a fraction of full scale
maxdiff() {
    od -An -v -tu1 "$1.rgb" | tr -s ' ' '\n' | sed '/^$/d' >a.txt
    od -An -v -tu1 "$2.rgb" | tr -s ' ' '\n' | sed '/^$/d' >b.txt
    paste a.txt b.txt | awk '{ if (NR % 2) h = $1 - $2; else { d = h * 256 + $1 - $2; if (d < 0) d = -d; if (d > m) m = d } } END { print m / 65535 }'
}
fail() { echo "trace_limit: $*" >&2; exit 1; }
below() { awk -v a="$2" -v b="$3" 'BEGIN { exit !(a < b) }' || fail "$1: $2 is not below $3"; }
render old
render limit Declare=Version=4.0
render straight Declare=Version=4.0 Declare=Ior=1
render deep Declare=Version=4.0 Declare=Depth=20
render mirror Declare=Version=4.0 Declare=Mirror=1
render one Declare=Version=4.0 +wt1
below "before 4.0 the mean at the limit" "$(mean old)" 0.001
below "4.0 at the limit lets too little through, mean" 0.1 "$(mean limit)"
# past the limit the ray ignores refraction: it sees the checker where unbent rays do, not where the full trace bends it
below "the limit against unbent rays" "$(maxdiff limit straight)" 0.001
below "the limit against the full trace" 0.1 "$(maxdiff limit deep)"
# past the limit no reflection is traced, so conserve_energy takes nothing from the light passing through
below "the limit with reflecting slabs" "$(maxdiff limit mirror)" 0.001
cmp -s limit.rgb one.rgb || fail "one thread differs from two"
# a ray continued through 600 surfaces stops black after 256, past the limit and through unbent surfaces before 4.0
render chain Declare=Version=4.0 Declare=Count=300 Declare=Tilt=0
render chain_old Declare=Count=300 Declare=Tilt=0 Declare=Ior=1
below "a long chain past the limit, mean" "$(mean chain)" 0.001
below "a long unbent chain before 4.0, mean" "$(mean chain_old)" 0.001
echo "trace_limit: a ray at max_trace_level goes straight on from 4.0 and is black before"
