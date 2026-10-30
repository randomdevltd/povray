#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
scene=$SRCDIR/tests/render/volume_legacy_media.pov
render() {
    name=$1; shift
    "$POVRAY" +i"$scene" +w8 +h8 +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 384 "$name.ppm" >"$name.rgb"
}
render legal
render quiet Declare=Interaction=0 Declare=Scatter=0.5
. "$SRCDIR/tests/render/image.sh"
image_awk '
function power(name,    v) { return sum_of(v, read_words(name ".rgb", v, 192)) }
BEGIN {
    if (!(power("legal") > 0)) fail("volume_legacy_media: legal is black")
    if (!(power("quiet") > 0)) fail("volume_legacy_media: quiet is black")
}'
write() {
    { if [ "$2" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
      echo 'global_settings { assumed_gamma 1 }'
      printf '%s\n' "$3"
    } >"$1.$2"
}
reject() {
    name=$1; message=$2; body=$3
    for language in pov pov4; do
        write invalid "$language" "$body"
        if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            echo "volume_legacy_media: expected $name in $language to fail" >&2; exit 1
        fi
        expected=$(printf '%s' "$message" | sed "s/LANG/$language/g")
        tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ' | grep -F "$expected" >/dev/null || { cat "$name.$language.log"; exit 1; }
    done
}
light='sphere { 0, 1 hollow pigment { rgbt 1 } interior { light {} } }'
reject own_glow 'sphere light block at line 3 of invalid.LANG holds the method 3 medium at line 3 of invalid.LANG' \
    'sphere { 0, 1 hollow pigment { rgbt 1 } interior { media { method 3 emission 1 } light {} } }'
reject own_absorption 'holds the method 1 medium' \
    'sphere { 0, 1 hollow pigment { rgbt 1 } interior { light { emission } media { method 1 absorption 1 } } }'
reject scatter 'The method 3 medium at line 4 of invalid.LANG in the box scatters, and the sphere light block at line 3 of invalid.LANG lights media' \
    "$light
box { -3, 3 hollow pigment { rgbt 1 } interior { media { method 3 scattering { 1, 0.1 } } } }"
reject atmosphere 'medium at line 4 of invalid.LANG in the atmosphere scatters' \
    "sphere { 0, 1 pigment { rgb 1 } light {} }
media { method 2 scattering { 1, 0.1 } }"
reject surface_sslt 'sphere light block at line 3 of invalid.LANG is on a subsurface method 1 finish' \
    'sphere { 0, 1 pigment { rgb 1 } finish { subsurface { translucency 0.5 method 1 } } light {} }
global_settings { subsurface { } }'
reject bounce_sslt 'is on a subsurface method 1 finish' \
    'global_settings { subsurface { method 1 } }
sphere { 0, 1 pigment { rgb 1 } finish { emission 0 subsurface { translucency 0.5 } } light { bounce } }'
reject volume_sslt 'sphere light block at line 4 of invalid.LANG is on a subsurface method 1 finish' \
    'global_settings { subsurface { method 1 } }
sphere { 0, 1 hollow pigment { rgbt 1 } finish { subsurface { translucency 0.5 } } interior { light {} } }'
sslt='box { -3, -2 pigment { rgb 1 }
  finish { subsurface { translucency 0.5 } } }'
reject lit_sslt 'The subsurface method 1 finish at line 5 of invalid.LANG on the box is lit by the sphere light block at line 6 of invalid.LANG' \
    "global_settings { subsurface { method 1 } }
$sslt
sphere { 0, 1 pigment { rgb 1 } light {} }"
reject lit_own_method 'finish at line 4 of invalid.LANG on the box is lit by the sphere light block at line 3 of invalid.LANG' \
    "$light
box { -3, -2 pigment { rgb 1 } finish { subsurface { method 1 translucency 0.5 } } }
global_settings { subsurface { } }"
reject mesh_sslt 'The subsurface method 1 finish at line 5 of invalid.LANG on the mesh is lit by the sphere light block at line 3' \
    "$light
mesh2 { vertex_vectors { 3, <-3, -3, -3>, <-2, -3, -3>, <-3, -2, -3> } texture_list { 1,
  texture { pigment { rgb 1 } finish { subsurface { method 1 translucency 0.5 } } } } face_indices { 1, <0, 1, 2>, 0 } inside_vector y }
global_settings { subsurface { } }"
inc_pov=glow.inc; inc_pov4=glow.inc4
for language in pov pov4; do
    eval inc=\$inc_$language
    printf '%s\n' '// line 1' 'sphere { 0, 1 hollow pigment { rgbt 1 }' '  interior { media { method 3 emission 1 } light {} } }' >"$inc"
    if [ "$language" = pov ]; then line="#include \"$inc\""; else line="include \"$inc\";"; fi
    write included "$language" "$line"
    if "$POVRAY" +i"included.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"included.$language.log" 2>&1; then
        echo "volume_legacy_media: expected included in $language to fail" >&2; exit 1
    fi
    tr '\n' ' ' <"included.$language.log" | tr -s '[:space:]' ' ' |
        grep -F "light block at line 3 of $inc holds the method 3 medium at line 3 of $inc;" >/dev/null ||
        { cat "included.$language.log"; exit 1; }
done
for language in pov pov4; do
    write unlit "$language" "global_settings { subsurface { method 1 } }
$sslt"
    "$POVRAY" +iunlit.$language +w1 +h1 +wt1 +pr -f -d -p -v -gp >unlit.$language.log 2>&1 || { cat unlit.$language.log; exit 1; }
done
for language in pov pov4; do
    for case in mixed alone; do
        varying=; [ "$case" = mixed ] && varying='media { method 4 absorption 0.5 density { bozo } }'
        write "$case" "$language" "box { -1, 1 hollow pigment { rgbt 1 }
  interior { media { method 4 resolution 0.1 absorption 0.5 } $varying } }"
        "$POVRAY" +i"$case.$language" +w1 +h1 +wt1 +pr -f -d -p -gp >"$case.$language.log" 2>&1 || { cat "$case.$language.log"; exit 1; }
    done
    tr '\n' ' ' <"mixed.$language.log" | tr -s '[:space:]' ' ' |
        grep -F "The constant medium at line 4 of mixed.$language in the box gives a resolution, but the grid there follows the varying medium at line 4 of mixed.$language, so the resolution is ignored." >/dev/null ||
        { cat "mixed.$language.log"; exit 1; }
    if grep -F 'so the resolution is ignored' "alone.$language.log" >/dev/null; then
        cat "alone.$language.log"; exit 1
    fi
done
echo 'volume_legacy_media: ok'
