#!/usr/bin/env bash
# test.sh <povray> [<povray built with POV_PARSER_EXPERIMENTAL_OBJ_IMPORT=1>]: .povm loading, its tree cache and the
# converter's round trips, each render compared pixel for pixel. Run as a user who cannot write a read-only directory.
set -uo pipefail
POV=$(realpath "$1"); POVOBJ=${2:+$(realpath "$2")}
T=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d)
trap 'chmod -R u+w "$W"; rm -rf "$W"' EXIT
cd "$W" || exit 1
node "$T/fixtures.mjs" "$W" || exit 1
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { local name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }

scene() { # scene <name> <mesh declaration> [more objects]
  cat > "$1.pov" <<EOF
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <0.5, 0.9, -0.6> look_at <0.5, 0, 0.5> angle 60 }
light_source { <-3, 5, -4> rgb 1 }
$2
object { M pigment { uv_mapping checker rgb 1 rgb <0.8, 0.3, 0.2> scale 0.1 } finish { specular 0.4 } }
${3:-}
EOF
}
render() { # render <povray> <name> [dir]: <name>.ppm and <name>.log; true if it rendered
  (cd "${3:-$W}" && "$1" +I"$W/$2.pov" +O"$W/$2.ppm" +FP +W120 +H90 -A +WT1 -D -V +L"$W" > "$W/$2.log" 2>&1)
}
same() { [ -s "$1.ppm" ] && [ -s "$2.ppm" ] && cmp -s <(tail -c 32400 "$1.ppm") <(tail -c 32400 "$2.ppm"); }
logged() { tr '\n' ' ' < "$1.log" | tr -s ' ' | grep -q "$2"; }
field() { od -An -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'; }
patch() { printf "$(printf '\\x%s' $(echo "$3" | sed 's/../& /g'))" | dd of="$1" bs=1 seek="$2" conv=notrunc status=none; }
flip() { patch "$1" "$2" "$(printf '%02x' $((0x$(field "$1" "$2" 1) ^ 0xff)))"; }

scene text2 '#include "mesh2.inc"'
scene text1 '#include "mesh1.inc"'
render "$POV" text2 && render "$POV" text1 || { echo "text renders failed"; cat text2.log text1.log; exit 1; }

scene writer '#declare M = mesh2 { povm "writer.povm" }'
check "writePovm output renders as its text mesh2" eval 'render "$POV" writer && same writer text2'

node "$T/../convert.mjs" mesh2.inc c2.povm > /dev/null && scene c2 '#declare M = mesh2 { povm "c2.povm" }'
check "converted mesh2 renders as the text mesh2" eval 'render "$POV" c2 && same c2 text2'
node "$T/../convert.mjs" mesh1.inc c1.povm > /dev/null && scene c1 '#declare M = mesh2 { povm "c1.povm" }'
check "converted mesh {} renders as the text mesh {}" eval 'render "$POV" c1 && same c1 text1'
if [ -n "$POVOBJ" ]; then
  solid='intersection { object { M } sphere { <0.5, -0.3, 0.5>, 0.45 } pigment { rgb <0.2, 0.5, 0.9> } translate <0.25, 0, 0.2> }'
  scene objimport '#declare M = mesh { obj "mesh.obj" inside_vector y }' "$solid"
  node "$T/../convert.mjs" mesh.obj co.povm > /dev/null 2>&1 && scene co '#declare M = mesh2 { povm "co.povm" inside_vector y }' "$solid"
  check "converted OBJ renders as POV-Ray's own OBJ import" eval 'render "$POVOBJ" objimport && render "$POVOBJ" co && same co objimport'
fi

cp c2.povt fresh.povt; inode=$(stat -c %i c2.povt)
check "a warm cache is read, not rewritten" eval 'render "$POV" c2 && same c2 text2 && [ "$(stat -c %i c2.povt)" = "$inode" ]'

patch c2.povt 16 0000000000000000
check "a cache from another build is rebuilt silently" eval 'render "$POV" c2 && same c2 text2 && cmp -s c2.povt fresh.povt && ! logged c2 "tree cache"'

flip c2.povt 200
check "a corrupt cache is rebuilt with a warning" eval 'render "$POV" c2 && same c2 text2 && cmp -s c2.povt fresh.povt && logged c2 "is corrupt"'

rm c2.povt; touch -d '10 seconds ago' c2.povt.tmp
check "a recent .tmp is left to its writer, with a warning" eval 'render "$POV" c2 && same c2 text2 && [ ! -e c2.povt ] && logged c2 "another render is writing"'
touch -d '20 minutes ago' c2.povt.tmp
check "a .tmp left by a crash is replaced" eval 'render "$POV" c2 && cmp -s c2.povt fresh.povt && [ ! -e c2.povt.tmp ] && ! logged c2 "tree cache"'
rm c2.povt; touch -d '+1 hour' c2.povt.tmp
check "a .tmp dated in the future is replaced" eval 'render "$POV" c2 && cmp -s c2.povt fresh.povt && [ ! -e c2.povt.tmp ]'

rm c2.povt; cp c2.pov c2b.pov
render "$POV" c2 & render "$POV" c2b; wait
check "two renders building the cache at once agree" eval 'same c2 text2 && same c2b text2 && cmp -s c2.povt fresh.povt && [ ! -e c2.povt.tmp ]'

cp c2.povm moved.povm && patch moved.povm $((28 + 12 * 312 + 4)) 9a99993e
cp c2.povt moved.povt
scene moved '#declare M = mesh2 { povm "moved.povm" }'
render "$POV" moved && ! cmp -s moved.povt fresh.povt && mv moved.ppm stale.ppm && rm moved.povt
check "an edited .povm of the same size rebuilds its cache" eval 'render "$POV" moved && same moved stale && ! same moved text2'

mkdir ro && cp c2.povm ro/ && chmod a-w ro
scene ro '#declare M = mesh2 { povm "ro/c2.povm" }'
if [ -w ro ]; then
  echo "skip a read-only directory: this user can write it"
else
  check "a read-only directory builds the tree in memory" eval 'render "$POV" ro && same ro text2 && [ ! -e ro/c2.povt ] && logged ro "Cannot write mesh tree cache"'
fi

refuses() { # refuses <name> <message> <file>: the scene loading <file> stops with <message>
  scene "$1" "#declare M = mesh2 { povm \"$3\" }"
  ! render "$POV" "$1" && logged "$1" "$2"
}
cp c2.povm magic.povm && patch magic.povm 0 504f5658
check "a bad magic is a parse error" refuses magic "is not a povm file" magic.povm
head -c -4 c2.povm > short.povm
check "a truncated file is a parse error" refuses short "bytes where its header needs" short.povm
cp c2.povm range.povm && patch range.povm $((28 + 12 * (625 + 625) + 8 * 625)) 71020000
check "an out-of-range face index is a parse error" refuses range "face index out of range" range.povm
cp c2.povm nan.povm && patch nan.povm $((28 + 12 * 625 + 4)) 0000c07f
check "a normal that is not a number is a parse error" refuses nan "normal is infinite or not a number" nan.povm

[ $fails -eq 0 ] && echo "all passed" || echo "$fails failed"
exit $fails
