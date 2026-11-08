#!/bin/sh
# csg_child_tree.sh <povray> <srcdir>: large CSGs render the same through their child trees as through the linear scan,
# with one thread or four, in the classic and the 4.0 language.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=160; H=120
# render <scene> <name> <POV_CSG_CHILD_TREE: empty = default, 0 = linear, 2 = every CSG of two or more> <threads>
render() {
    POV_CSG_CHILD_TREE=$3 "$POVRAY" +i"$SRCDIR/tests/render/$1" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$2.ppm" +WT$4 \
        > "$2.log" 2>&1 || { cat "$2.log"; echo "csg_child_tree: $2 failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$2.ppm" > "$2.pixels"
}
same() {
    cmp -s "$1.pixels" "$2.pixels" || { echo "csg_child_tree: $1 and $2 differ" >&2; exit 1; }
}
for scene in csg_child_tree.pov csg_child_tree.pov4; do
    render $scene linear 0 1
    render $scene default "" 1
    render $scene every 2 1
    render $scene threads "" 4
    grep -q "CSG child trees: [1-9]" default.log || { cat default.log; echo "csg_child_tree: $scene built no child tree" >&2; exit 1; }
    ! grep -q "CSG child trees" linear.log || { echo "csg_child_tree: POV_CSG_CHILD_TREE=0 still built trees" >&2; exit 1; }
    same linear default
    same linear every
    same linear threads
    echo "csg_child_tree: $scene renders the same through child trees and the linear scan"
done
