#!/bin/sh
# light_group_global_lights.sh <povray> <srcdir>: a 4.0 light_group takes global_lights. With it on, the scene's
# light_source outside the groups (blue) reaches the panel in each group; off, it does not; and neither
# group's interior light (red on the left, green on the right) reaches the other group's panel.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=6; H=2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/light_group_global_lights.pov4" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; echo "light_group_global_lights: $name failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
# check <name> <left panel R G B> <right panel R G B>: each expectation is 0 (exactly unlit) or + (lit).
# The panels' centres are columns 1 and 4 of each row.
check() {
    od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk -v name="$1" -v want="$2 $3 $4 $5 $6 $7" -v w=$W '
        { col = (NR - 1) % w; if (col == 1) for (c = 1; c <= 3; c++) v[c] += $c; if (col == 4) for (c = 1; c <= 3; c++) v[c + 3] += $c }
        END {
            split(want, e, " ")
            for (i = 1; i <= 6; i++) {
                if ((e[i] == "0" && v[i] != 0) || (e[i] == "+" && v[i] < 2000)) {
                    printf "light_group_global_lights: %s: %s panel %s channel is %d, expected %s\n", name,
                        (i <= 3 ? "left" : "right"), substr("RGBRGB", i, 1), v[i], (e[i] == "0" ? "0" : "lit") | "cat 1>&2"
                    bad = 1
                }
            }
            exit bad
        }' || exit 1
}
render on Declare=Global=1
render off Declare=Global=0
check on + 0 + 0 + +
check off + 0 0 0 + 0
echo "light_group_global_lights: global_lights on lets the outside light into both groups, off keeps it out, and the groups' own lights stay apart"
