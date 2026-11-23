#!/bin/sh
# csg_child_tree_parse.sh <povray> <srcdir>: parse-time trace() and inside() on large CSGs answer the same through child trees
# as through the linear scan, to the last bit.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
# parse <name> <POV_CSG_CHILD_TREE: empty = default, 0 = linear, 2 = every CSG of two or more>
parse() {
    POV_CSG_CHILD_TREE=$2 "$POVRAY" +i"$SRCDIR/tests/render/csg_child_tree_parse.pov" +w8 +h8 -a -d -p -v -f \
        > "$1.log" 2>&1 || { cat "$1.log"; echo "csg_child_tree_parse: $1 failed" >&2; exit 1; }
    mv csg_child_tree_parse.txt "$1.txt"
}
parse linear 0
parse default ""
parse every 2
grep -q "CSG child trees: [1-9][0-9]* made for parse-time" default.log ||
    { cat default.log; echo "csg_child_tree_parse: the queries made no child tree" >&2; exit 1; }
! grep -q "CSG child trees" linear.log || { echo "csg_child_tree_parse: POV_CSG_CHILD_TREE=0 still made trees" >&2; exit 1; }
[ "$(wc -l < linear.txt)" -eq 3100 ] || { echo "csg_child_tree_parse: expected 3100 queries" >&2; exit 1; }
cmp -s linear.txt default.txt || { echo "csg_child_tree_parse: child trees changed a parse-time answer" >&2; exit 1; }
cmp -s linear.txt every.txt || { echo "csg_child_tree_parse: trees on small CSGs changed a parse-time answer" >&2; exit 1; }
echo "csg_child_tree_parse: parse-time trace() and inside() answer the same through child trees and the linear scan"
