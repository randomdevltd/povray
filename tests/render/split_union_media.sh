#!/bin/sh
# split_union_media.sh <povray> <srcdir>: a union whose prepared medium or media light names it stays whole and renders as it.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=48; H=24
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/split_union_media.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
# objects <name> <count>: the finite objects placed, 1 when the union stays whole (the plane is infinite)
objects() {
    got=$(sed -n 's/.*Finite Objects: *\([0-9]*\).*/\1/p' "$1.log")
    [ "$got" = "$2" ] || { echo "split_union_media: $1 places $got finite objects, expected $2" >&2; exit 1; }
}
quiet() {
    if grep -q "gives no light" "$1.log"; then echo "split_union_media: $1 made no media light" >&2; exit 1; fi
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"; done
    cmp -s "$1.pixels" "$2.pixels" || { echo "split_union_media: $1 differs from $2" >&2; exit 1; }
}
for threads in 1 4; do
    for medium in "Lit=0" "Lit=1" "Lit=1 Declare=Constant=1"; do
        tag=$(echo "$medium" | tr -dc 'a-zA-Z0-9')_$threads
        render split_$tag Declare=Version=4.0 Declare=$medium +WT$threads
        render whole_$tag Declare=Version=4.0 Declare=$medium Declare=Whole=1 +WT$threads
        objects split_$tag 1
        objects whole_$tag 1
        same split_$tag whole_$tag
        case $medium in Lit=1*) quiet split_$tag; quiet whole_$tag;; esac
    done
done
# nest <n> <finite objects>: the union inside a splitting union (1) or a light_group (2) still stays whole
for nest in "1 2" "2 1"; do
    set -- $nest
    render nest_split_$1 Declare=Version=4.0 Declare=Lit=1 Declare=Nest=$1 +WT4
    render nest_whole_$1 Declare=Version=4.0 Declare=Lit=1 Declare=Nest=$1 Declare=Whole=1 +WT4
    objects nest_split_$1 $2
    objects nest_whole_$1 $2
    quiet nest_split_$1
    same nest_split_$1 nest_whole_$1
done
render classic Declare=Version=3.7
objects classic 2
echo "split_union_media: unions holding prepared media or media lights stay whole; classic media unions still split"
