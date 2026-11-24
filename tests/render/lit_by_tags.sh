#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
SCENE="$SRCDIR/tests/render/lit_by_tags.pov"
render() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$scene" +w32 +h24 +wt2 +pr -a -d -p -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 4608 "$name.ppm" >"$name.rgb"
}
fail() { echo "lit_by_tags: $*" >&2; exit 1; }
same() { cmp -s "$1.rgb" "$2.rgb" || fail "$1 and $2 differ"; }
mean() { od -An -v -tu1 "$1.rgb" | awk '{ for (i = 1; i <= NF; i++) if (n++ % 2) s += hi * 256 + $i; else hi = $i } END { print s / (n / 2) / 65535 }'; }
refuses() {
    name=$1; text=$2
    if "$POVRAY" +i"$name.pov" +w8 +h6 -d -p -f >"$name.log" 2>&1; then fail "$name rendered"; fi
    grep -q "$text" "$name.log" || { cat "$name.log"; fail "$name did not say: $text"; }
}

# a surface that leaves out a lamp and a surface light (by its object's tags) renders as if neither were there
render surface "$SCENE" Declare=Case=1
render surface_alone "$SCENE" Declare=Case=1 Declare=Query=0 Declare=NoB=1
same surface surface_alone
render surface_all "$SCENE" Declare=Case=1 Declare=Query=0
cmp -s surface.rgb surface_all.rgb && fail "the floor's query left every light in"
# the subsurface takes a lamp its own surface leaves out; kept to the surface's lamp, it is as if the other were not there
render subsurface "$SCENE" Declare=Case=2
render subsurface_a "$SCENE" Declare=Case=2 Declare=Query=0
render subsurface_alone "$SCENE" Declare=Case=2 Declare=Query=0 Declare=NoB=1
same subsurface_a subsurface_alone
awk -v a="$(mean subsurface)" -v b="$(mean subsurface_a)" 'BEGIN { exit !(a > b) }' ||
    fail "the subsurface lit by both lamps ($(mean subsurface)) is not brighter than by one ($(mean subsurface_a))"
# a medium scatters only the lights its query selects
render media "$SCENE" Declare=Case=3
render media_alone "$SCENE" Declare=Case=3 Declare=Query=0 Declare=NoB=1
same media media_alone
# an object's query is inherited by its children and overridden by a finish's own; the 4.0 language lowers to the same
render inherit "$SCENE" Declare=Case=4
render explicit "$SCENE" Declare=Case=4 Declare=Query=0
same inherit explicit
render pov4 "$SRCDIR/tests/render/lit_by_tags.pov4"
same inherit pov4
# a light the camera's filter leaves out lights nothing in that view, whatever a query selects
render filtered "$SCENE" Declare=Case=5
render unlit "$SCENE" Declare=Case=5 Declare=Query=0
same filtered unlit
render unfiltered "$SCENE" Declare=Case=4 Declare=Query=0 'Filter_Tags=!"b"'
cmp -s explicit.rgb unfiltered.rgb && fail "the command-line filter left the lamp it removed lighting the right sphere"
# a light group and a query both decide: the member takes only the group's and global lights its query also selects
render group "$SCENE" Declare=Case=6
render group_alone "$SCENE" Declare=Case=6 Declare=NoB=1 Declare=NoD=1
same group group_alone
render group_all "$SCENE" Declare=Case=6 Declare=Query=0
cmp -s group.rgb group_all.rgb && fail "the group member's query left every light in"

# failures name what is wrong
printf '#version 4.0;\nlight_source { 0, 1 lit_by_tags { "a" } }\n' >on_light.pov
refuses on_light "a light_source is not lit"
printf '#version 4.0;\nsphere { 0, 1 finish { lit_by_tags { "a" & } } }\n' >malformed.pov
refuses malformed "Invalid lit_by_tags expression"
