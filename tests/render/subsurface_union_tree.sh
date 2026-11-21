#!/bin/sh
# A subsurface body of ten members, which its union indexes in a child tree, renders as it does when the union asks
# every member in turn: per-member flesh and seam walks (case 0) and a volume light's exit search (case 1) take the
# tree's paths.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
render() {
    arm=$1
    shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_union_tree.pov" +w8 +h4 +wt"${THREADS:-1}" +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$arm.ppm" "$@" >"$arm.log" 2>&1 || { cat "$arm.log"; exit 1; }
}
for case in 0 1; do
    render tree$case Declare=Case=$case
    THREADS=4 render parallel$case Declare=Case=$case
    POV_CSG_CHILD_TREE=0 render scan$case Declare=Case=$case
    tr -d '\r' < tree$case.log | grep -q "CSG child trees: 1 CSG" ||
        { cat tree$case.log; echo "subsurface_union_tree: case $case built no child tree" >&2; exit 1; }
    if tr -d '\r' < scan$case.log | grep -q "CSG child trees:"; then
        cat scan$case.log; echo "subsurface_union_tree: case $case built a child tree with the trees off" >&2; exit 1
    fi
    for arm in tree parallel scan; do cp $arm$case.ppm $arm.ppm; done
    image_awk "
    BEGIN { CASE = $case }"'
    BEGIN {
        n = read_ppm("tree.ppm", tree)
        read_ppm("parallel.ppm", parallel)
        read_ppm("scan.ppm", scan)
        if (n != 96) fail("expected 8x4 RGB pixels")
        for (i = 0; i < n; ++i) {
            if (!(tree[i] > 0)) fail("case " CASE ": a pixel of the lit body is dark")
            if (parallel[i] != tree[i]) fail("case " CASE ": worker count changed the output")
            if (scan[i] != tree[i]) fail("case " CASE ": pixel " int(i / 3) " with the child tree is " tree[i] ", asking every member " scan[i])
        }
    }'
done
echo "subsurface_union_tree: pass"
