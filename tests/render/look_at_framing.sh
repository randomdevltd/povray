#!/bin/sh
# look_at_framing.sh <povray> <srcdir>: framed cameras match their explicit equivalents; bad targets fail.
set -eu
POVRAY=$1; SRCDIR=$2; SCENE=$SRCDIR/tests/render/look_at_framing.pov
render() {
    name=$1; scene=$2; size=$3; shift 3
    "$POVRAY" +i"$scene" +L"$SRCDIR/tests/pov4" +w$size +h$size -a -d -p -v -gp +wt1 +fp16 File_Gamma=1 +o"look_at_$name.ppm" "$@" \
        > "look_at_$name.log" 2>&1 || { cat "look_at_$name.log"; exit 1; }
    tail -c $((size * size * 6)) "look_at_$name.ppm" > "look_at_$name.px"
}
equal() {
    cmp "look_at_$1.px" "look_at_$2.px" || { echo "look_at framing mismatch: $1 / $2" >&2; exit 1; }
}
warns() {
    tr -d '\n' < "look_at_$1.log" | grep -q "$2" || { cat "look_at_$1.log"; echo "look_at $1: missing warning" >&2; exit 1; }
}
quiet() {
    if tr -d '\n' < "look_at_$1.log" | grep -q "Warning: look_at"; then cat "look_at_$1.log"; echo "look_at $1: unexpected warning" >&2; exit 1; fi
}
fails() {
    if "$POVRAY" +i"$SCENE" +w16 +h16 -d -p -gp -f Declare=Case=$1 > "look_at_fail$1.log" 2>&1; then
        echo "look_at case $1: expected a parse error" >&2; exit 1
    fi
    tr -d '\n' < "look_at_fail$1.log" | grep -q "$2" || { cat "look_at_fail$1.log"; exit 1; }
}
for c in 0 3 6 9 10 11 12 14 15 16 17 23 26 28 29 30 31 32 33 34 36 39 40 41 42 45 46 47 48 49 50 51 52 53 54 \
         60 61 62 63 64 65 66 67 68 69 77 78; do
    render "case$c" "$SCENE" 64 Declare=Case=$c
    render "ref$c" "$SCENE" 64 Declare=Case=$c Declare=Reference=1
    equal "case$c" "ref$c"
done
for c in 1 2 24 37 44; do
    render "case$c" "$SCENE" 64 Declare=Case=$c
    equal "case$c" ref0
done
# framing never moves the camera: each projection equals an explicit camera written at the same location
# (perspective is case 0 and user_defined case 24 above; 72-74 are orthographic targets around, behind and just ahead)
for c in 4 72 73 74 18 19 5 20 75 13 76 21 22 57; do
    render "case$c" "$SCENE" 64 Declare=Case=$c
    render "ref$c" "$SCENE" 64 Declare=Case=$c Declare=Reference=1
    equal "case$c" "ref$c"
done
render case43 "$SCENE" 64 Declare=Case=43 'Filter_Tags=!"far"'
equal case43 ref0
render case43_unfiltered "$SCENE" 64 Declare=Case=43
if cmp -s look_at_case43.px look_at_case43_unfiltered.px; then echo "look_at case 43: Filter_Tags changed nothing" >&2; exit 1; fi
warns case2 "skipped 1 unbounded object"
warns case6 "does not fit the camera's field"
warns case36 "does not fit the camera's field"
warns case72 "does not fit the camera's field"
warns case73 "does not fit the camera's field"
warns case39 "skipped 1 unbounded object"
warns case44 "tagged selects no object placed so far"
warns case65 "far mouths coincide"
# copies of groups and portals add what a hand-written copy adds, nothing twice
for c in 50 61 62 64 65; do
    for line in 'Objects:' 'Light Sources:'; do
        [ "$(grep "$line" "look_at_case$c.log")" = "$(grep "$line" "look_at_ref$c.log")" ] || { echo "look_at case $c: $line differ" >&2; exit 1; }
    done
done
[ "$(grep 'Objects:' look_at_case44.log)" = "$(grep 'Objects:' look_at_ref0.log)" ] || { echo "look_at case 44: an empty selection added an object" >&2; exit 1; }
render case70 "$SCENE" 64 Declare=Case=70
equal case70 ref0
render case71 "$SCENE" 64 Declare=Case=71
render ref71 "$SCENE" 64 Declare=Case=71 Declare=Reference=1
equal case71 ref71
render case71_unplaced "$SCENE" 64 Declare=Case=71 Declare=Unplaced=1
for c in case71 ref71 case71_unplaced; do warns $c "0 built for parse-time queries, 1 built for the scene"; done
render case58 "$SCENE" 64 Declare=Case=58
[ "$(tr -d '\n' < look_at_case58.log | grep -o "does not fit" | wc -l)" -eq 1 ] || { cat look_at_case58.log; echo "look_at case 58: expected one warning" >&2; exit 1; }
for c in 0 9 10 11 12 13 14 15 16 17 18 19 20 21 22 26 29 30 31 33 34 37 40 41 42 43 45 46 47 48 50 51 52 53 54 57 66 67 68 69 74 75 76 77 78; do quiet "case$c"; done
[ "$(tr -d '\000' < look_at_case12.px | wc -c)" -gt 0 ] || { echo "look_at: the tiny target rendered nothing" >&2; exit 1; }
fails 7 "look_at object framing needs finite non-empty bounds"
fails 8 "look_at tagged selects no object with finite non-empty bounds"
fails 25 "cannot frame a target with a mesh_camera"
fails 35 "look_at frame multiplier must be a finite number greater than 0"
fails 27 "undeclared identifier 'tagged'"
fails 38 "look_at tagged selects nothing"
fails 55 "look_at found no ray of this user_defined camera"
fails 56 "look_at could not frame these bounds with this camera projection"
fails 59 "look_at object framing needs finite non-empty bounds"
PROJ=$SRCDIR/tests/render/look_at_framing_projections.pov
project() {
    name=$1; shift
    "$POVRAY" +i"$PROJ" +L"$SRCDIR/distribution/include" +w128 +h64 -a -d -p -v -gp +wt1 +fp16 File_Gamma=1 +o"look_at_$name.ppm" "$@" \
        > "look_at_$name.log" 2>&1 || { cat "look_at_$name.log"; exit 1; }
    tail -c 49152 "look_at_$name.ppm" > "look_at_$name.px"
}
# Red pixels' extent: the target must sit at the image centre and fill Multiplier of the limiting axis (one pixel slack).
framed() {
    od -An -v -tu1 -w6 "look_at_$1.px" | awk -v m="$2" -v centred="$3" -v name="$1" '
        $1 > 128 && $3 < 80 { p = NR - 1; x = p % 128; y = int(p / 128); n++
            if (n == 1 || x < x0) x0 = x; if (n == 1 || x > x1) x1 = x; if (n == 1 || y < y0) y0 = y; if (n == 1 || y > y1) y1 = y }
        END {
            if (!n) { print name ": target not visible" > "/dev/stderr"; exit 1 }
            fx = (x1 - x0 + 1) / 128; fy = (y1 - y0 + 1) / 64; f = (fx > fy) ? fx : fy; slack = (fx > fy) ? 1.5 / 128 : 1.5 / 64
            bad = (f < m - slack || f > m + slack) || ((x0 + x1) / 2 - 63.5) ^ 2 > 2.25 || (centred && ((y0 + y1) / 2 - 31.5) ^ 2 > 2.25)
            if (bad) { printf "%s: red x %d-%d y %d-%d, expected %.2f of the image, centred\n", name, x0, x1, y0, y1, m > "/dev/stderr"; exit 1 } }'
}
for c in 0 1 2 5 6; do project "proj$c" Declare=Case=$c; framed "proj$c" 0.9 1; done
project proj4 Declare=Case=4; framed proj4 0.6 1
project proj3 Declare=Case=3
warns proj3 "does not fit the camera's field"
od -An -v -tu1 -w6 look_at_proj3.px | awk '$1 > 128 && $3 < 80 { x = (NR - 1) % 128; s += x; n++ } END { if (!n || (s / n - 63.5) ^ 2 > 4) { print "proj3: target not centred across" > "/dev/stderr"; exit 1 } }'
project proj0_unframed Declare=Case=0 Declare=Frame=0
if cmp -s look_at_proj0.px look_at_proj0_unframed.px; then echo "proj0: framing changed nothing" >&2; exit 1; fi
for c in 0 6; do project "proj${c}_tagged" Declare=Case=$c Declare=Frame=2; equal "proj${c}_tagged" "proj$c"; done
for c in 0 5; do project "proj${c}_area" Declare=Case=$c Declare=Frame=3; equal "proj${c}_area" "proj$c"; done
project proj0_turned Declare=Case=0 Declare=Frame=4; equal proj0_turned proj0
# A point light turns the 360 degree view to it without narrowing the window: centred across, the same size.
project proj0_point Declare=Case=0 Declare=Frame=5
quiet proj0_point
red() { od -An -v -tu1 -w6 "look_at_$1.px" | awk '$1 > 128 && $3 < 80 { x = (NR - 1) % 128; s += x; n++ } END { print n + 0, (n ? s / n : -1) }'; }
echo "$(red proj0_point) $(red proj0_unframed)" | awk '{ n = $1; mean = $2; base = $3; if (!n || (mean - 63.5) ^ 2 > 4 || n < base * 0.95 || n > base * 1.05) {
    printf "proj0_point: %d red pixels centred at %.1f, expected about %d at 63.5\n", n, mean, base > "/dev/stderr"; exit 1 } }'
for twin in framing framing_tagged; do
    render "${twin}_new" "$SRCDIR/tests/pov4/$twin.pov4" 96
    render "${twin}_ref" "$SRCDIR/tests/pov4/$twin.pov" 96 Declare=Reference=1
    equal "${twin}_new" "${twin}_ref"
done
UNION=$SRCDIR/tests/render/look_at_framing_union.pov
for c in 0 1 2 3; do
    render "union$c" "$UNION" 128 Declare=Case=$c
    render "union_ref$c" "$UNION" 128 Declare=Case=$c Declare=Reference=1
    equal "union$c" "union_ref$c"
    quiet "union$c"
    od -An -v -tu1 -w6 "look_at_union$c.px" | awk '
        $1 > 128 && $3 < 80 { red++ }
        $3 > 128 && $1 < 80 { green++ }
        END { if (red < 100 || green < 100) { print "union components missing" > "/dev/stderr"; exit 1 } }'
done
rm -f look_at_*.ppm look_at_*.px look_at_*.log
echo "look_at framing: projections, transforms, tagged selections, focus and fallbacks match explicit cameras"
