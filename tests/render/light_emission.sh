#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/light_emission.pov" +w4 +h4 +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 96 "$name.ppm" >"$name.rgb"
}
same() { cmp "$1.rgb" "$2.rgb"; }
# close <a> <b>: every sample within 0.5% (or 2/65535); the alias and its hand-written field differ in their last bits, which move draws
close() {
    { od -An -v -tu1 "$1.rgb"; echo next; od -An -v -tu1 "$2.rgb"; } | awk -v pair="$1 $2" '
        $1 == "next" { second = 1; n = 0; next }
        { for (i = 1; i <= NF; i++) if (n++ % 2) { if (second) v[n] = hi * 256 + $i; else w[n] = hi * 256 + $i } else hi = $i }
        END { for (k in w) { a = w[k]; b = v[k]; d = a > b ? a - b : b - a; m = a > b ? a : b
                  if (d > 2 && d > 0.005 * m) bad++; if (m > 0 && d / m > worst) worst = d / m }
              printf "light_emission: %s differ by at most %.4f\n", pair, worst; exit bad > 0 }'
}
# pair <name> <args>: the alias and the manual field at many draws agree, and the alias keeps its 16-draw render
pair() {
    pair_name=$1; shift
    render "alias$pair_name" "$@"
    render "fine_alias$pair_name" Declare=Samples=1024 "$@"
    render "fine_manual$pair_name" Declare=Samples=1024 Declare=Alias=0 "$@"
    close "fine_alias$pair_name" "fine_manual$pair_name"
}
for method in 1 2 3 4; do
    for unit in 0 1; do
        pair "${method}_$unit" Declare=Method="$method" Declare=Lumens="$unit"
    done
done
render alias_parallel +wt2
same alias4_0 alias_parallel
for version in 3.6 3.7 3.8 4.0; do
    render "version$version" Declare=SceneVersion="$version"
    same alias4_0 "version$version"
done
for option in Constant OwnAbsorb Overlap; do
    pair "$option" Declare="$option"=1
done
"$POVRAY" +i"$SRCDIR/tests/render/light_emission.pov4" +w4 +h4 +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1 +opov4.ppm >pov4.log 2>&1 || { cat pov4.log; exit 1; }
tail -c 96 pov4.ppm >pov4.rgb
same aliasConstant pov4
pair _scale_nits Declare=Strength=100
# stated lumens hold whatever the field's strength; its last bits move draws, so the many-draw renders agree
render fine_scale_lumens Declare=Strength=100 Declare=Lumens=1 Declare=Samples=1024
close fine_alias4_1 fine_scale_lumens
render omit Declare=Omit=1
render omit_strong Declare=Omit=1 Declare=Strength=100
same omit omit_strong
render white Declare=Alias=0 Declare=Constant=1 Declare=White=1
same omit white
render zero Declare=Zero=1
render empty Declare=Empty=1
same zero empty
python3 - <<'PY'
from pathlib import Path
import struct
values=lambda n:struct.unpack('>48H',Path(n+'.rgb').read_bytes())
assert any(values('alias4_0'))
assert all(v==0 for v in values('zero'))
assert sum(values('aliasOverlap'))<sum(values('alias4_0'))
assert sum(values('aliasOwnAbsorb'))<sum(values('alias4_0'))
assert any(values('omit'))
PY
fail() {
    name=$1; message=$2; shift 2
    if "$POVRAY" +i"$SRCDIR/tests/render/light_emission.pov" +w1 +h1 +wt1 +pr -a -d -p -v -gp +o"$name.png" "$@" >"$name.log" 2>&1; then
        echo "light_emission: expected $name to fail" >&2; exit 1
    fi
    tr '\n' ' ' <"$name.log" | tr -s '[:space:]' ' ' | grep -F "$message" >/dev/null || { cat "$name.log"; exit 1; }
}
fail pigment_after 'one pigment or emission input' Declare=Conflict=1
fail emission_after 'one pigment or emission input' Declare=Alias=0 Declare=Conflict=2
fail duplicate 'one pigment or emission input' Declare=Conflict=3
fail zero_lumens 'generated-lumens integral is unresolved or zero' Declare=Zero=1 Declare=Lumens=1
fail empty_lumens 'generated-lumens integral is unresolved or zero' Declare=Empty=1 Declare=Lumens=1
echo 'light_emission: ok'
