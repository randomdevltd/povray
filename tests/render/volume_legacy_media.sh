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
reject() {
    name=$1; message=$2; body=$3
    for language in pov pov4; do
        { if [ "$language" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
          echo 'global_settings { assumed_gamma 1 }'
          printf '%s\n' "$body"
        } >"invalid.$language"
        if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            echo "volume_legacy_media: expected $name in $language to fail" >&2; exit 1
        fi
        tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ' | grep -F "$message" >/dev/null || { cat "$name.$language.log"; exit 1; }
    done
}
light='sphere { 0, 1 hollow pigment { rgbt 1 } interior { light {} } }'
reject own_glow 'sphere light block at line 3 holds the method 3 medium at line 3' \
    'sphere { 0, 1 hollow pigment { rgbt 1 } interior { media { method 3 emission 1 } light {} } }'
reject own_absorption 'holds the method 1 medium' \
    'sphere { 0, 1 hollow pigment { rgbt 1 } interior { light { emission } media { method 1 absorption 1 } } }'
reject scatter 'The method 3 medium at line 4 in the box scatters, and the sphere light block at line 3 lights media' \
    "$light
box { -3, 3 hollow pigment { rgbt 1 } interior { media { method 3 scattering { 1, 0.1 } } } }"
reject atmosphere 'medium at line 4 in the atmosphere scatters' \
    "sphere { 0, 1 pigment { rgb 1 } light {} }
media { method 2 scattering { 1, 0.1 } }"
reject surface_sslt 'sphere light block at line 3 is on a subsurface method 1 finish' \
    'sphere { 0, 1 pigment { rgb 1 } finish { subsurface { translucency 0.5 method 1 } } light {} }
global_settings { subsurface { } }'
reject bounce_sslt 'is on a subsurface method 1 finish' \
    'global_settings { subsurface { method 1 } }
sphere { 0, 1 pigment { rgb 1 } finish { emission 0 subsurface { translucency 0.5 } } light { bounce } }'
reject volume_sslt 'sphere light block at line 4 is on a subsurface method 1 finish' \
    'global_settings { subsurface { method 1 } }
sphere { 0, 1 hollow pigment { rgbt 1 } finish { subsurface { translucency 0.5 } } interior { light {} } }'
echo 'volume_legacy_media: ok'
