#!/bin/sh
# bounce_no_surface.sh <povray> <srcdir>: a bounce light on a difference that removes everything, and on a sphere clipped
# away, renders with a warning naming the object and applies no bounce.
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
for case in 0 1; do
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_no_surface.pov" +w8 +h6 -a -d -p -v +o"case$case.ppm" Declare=Case=$case \
        > "case$case.log" 2>&1 || { cat "case$case.log"; echo "bounce_no_surface: case $case failed to render" >&2; exit 1; }
    tr -d ' \r\n' < "case$case.log" | grep -q "Surfacebouncefoundnosurfaceonthe.*atline.*nobounceisapplied" ||
        { cat "case$case.log"; echo "bounce_no_surface: case $case did not warn naming its object" >&2; exit 1; }
done
echo "bounce_no_surface: pass"
