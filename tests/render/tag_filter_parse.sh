#!/bin/sh
# tag_filter_parse.sh <povray> <srcdir>: Filter_Tags acts after the parse. Parse-time tagged, trace(), inside() and extents
# answer as without the filter, and the filtered render equals a scene that never placed the filtered objects.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
fail() { echo "tag_filter_parse: $*" >&2; exit 1; }
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/tag_filter_parse.pov" +w64 +h32 -a -d -p -v -gp +wt1 +fp16 File_Gamma=1 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; fail "$name failed"; }
    tail -c $((64 * 32 * 6)) "$name.ppm" > "$name.px"
    if [ -f tag_filter_parse.txt ]; then mv tag_filter_parse.txt "$name.txt"; fi
}
render unfiltered
render filtered 'Filter_Tags=!"hidden*"'
render reference Declare=Reference=1
[ -s unfiltered.txt ] || fail "the queries wrote nothing"
cmp -s unfiltered.txt filtered.txt || { diff unfiltered.txt filtered.txt; fail "Filter_Tags changed a parse-time answer"; }
cmp -s filtered.px reference.px || fail "the filtered render differs from the scene without the filtered objects"
if cmp -s filtered.px unfiltered.px; then fail "Filter_Tags changed nothing in the render"; fi
for line in 'Objects:' 'Light Sources:'; do
    [ "$(grep "$line" filtered.log)" = "$(grep "$line" reference.log)" ] || fail "$line differ from the reference"
done
# scene-wide flags gathered from placed objects, such as whether any is opaque, come from those the filter keeps, each
# counted under the #version in force where it was placed
flags() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/$scene.pov" +w64 +h32 -a -d -p -v -gp +wt1 +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; fail "$name failed"; }
    tail -c $((64 * 32 * 6)) "$name.ppm" > "$name.px"
}
for scene in tag_filter_parse_flags tag_filter_parse_version; do
    flags flags_filtered 'Filter_Tags=!"hidden"'
    flags flags_reference Declare=Reference=1
    cmp -s flags_filtered.px flags_reference.px || fail "$scene: an object the filter removed still changed scene-wide settings"
done
echo "tag_filter_parse: parse-time queries ignore Filter_Tags, and the render leaves the filtered objects out"
