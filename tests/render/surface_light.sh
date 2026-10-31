#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; input=$2; shift 2
    "$POVRAY" +i"$input" +w8 +h8 +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 384 "$name.ppm" >"$name.rgb"
}
scene=$SRCDIR/tests/render/surface_light.pov
for geometry in 0 1 2 3 4 5 6 7; do
    count=32
    if [ "$geometry" = 7 ]; then count=512; fi
    render "geometry$geometry" "$scene" Declare=Case="$geometry" Declare=Samples="$count"
    render "native$geometry" "$SRCDIR/tests/render/surface_light.pov4" Declare=Case="$geometry" Declare=Samples="$count"
    if [ "$geometry" != 7 ]; then cmp "geometry$geometry.rgb" "native$geometry.rgb"; fi
done
for version in 3.6 3.7 3.8 4.0; do
    render "version$version" "$scene" Declare=SceneVersion="$version"
    cmp geometry0.rgb "version$version.rgb"
done
render pov4 "$SRCDIR/tests/render/surface_light.pov4"
cmp geometry0.rgb pov4.rgb
render alias "$scene" Declare=Case=5 Declare=Alias=1
cmp geometry5.rgb alias.rgb
render unitless "$scene" Declare=Units=2
cmp geometry0.rgb unitless.rgb
render native_alias "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=5 Declare=Alias=1
cmp alias.rgb native_alias.rgb
render lumens "$scene" Declare=Units=1
render native_lumens "$SRCDIR/tests/render/surface_light.pov4" Declare=Units=1
cmp lumens.rgb native_lumens.rgb
render few "$scene" Declare=Samples=16
render many "$scene" Declare=Samples=128
for bounce in 1 2 3 4; do
    render "bounce$bounce" "$scene" Declare=Bounce="$bounce"
    render "native_bounce$bounce" "$SRCDIR/tests/render/surface_light.pov4" Declare=Bounce="$bounce"
    cmp "bounce$bounce.rgb" "native_bounce$bounce.rgb"
done
cmp bounce1.rgb bounce2.rgb
render passive "$scene" Declare=Bounce=1 Declare=Passive=1
render native_passive "$SRCDIR/tests/render/surface_light.pov4" Declare=Bounce=1 Declare=Passive=1
cmp passive.rgb native_passive.rgb
render csg "$scene" Declare=Case=8 Declare=Bounce=3 Declare=Passive=1 Declare=Volume=1
render native_csg "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=8 Declare=Bounce=3 Declare=Passive=1 Declare=Volume=1
cmp csg.rgb native_csg.rgb
render portal "$scene" Declare=Case=9
render native_portal "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=9
cmp portal.rgb native_portal.rgb
{ echo '#version 3.7;'
  echo 'global_settings { assumed_gamma 1 }'
  echo 'camera { orthographic location <0,0,-5> look_at 0 right x*2.4 up y*2.4 }'
  echo 'sphere { 0,1 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } double_illuminate'
  echo '  light { pigment { rgb 1 } brightness 4 samples 64 } }'
} >self.pov
render self self.pov
python3 - <<'PY'
from pathlib import Path
import struct
power=lambda n:sum(struct.unpack('>192H',Path(n+'.rgb').read_bytes()))
for case in range(8):
    assert power('geometry'+str(case))>0,case
assert power('native7')>0
assert abs(power('native7')/power('geometry7')-1)<0.05
assert power('passive')==0
assert power('csg')>0
assert power('portal')>0
assert power('self')==0
reference=power('geometry0')
assert abs(power('lumens')/reference-1)<0.12
assert abs(power('few')/power('many')-1)<0.35
PY
reject() {
    name=$1; message=$2; body=$3
    for language in pov pov4; do
        { if [ "$language" = pov ]; then echo '#version 3.7;'; fi
          echo 'global_settings { assumed_gamma 1 }'
          echo "sphere { 0,1 $body }"
        } >"invalid.$language"
        if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            echo "surface_light: expected $name in $language to fail" >&2; exit 1
        fi
        tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ' | grep -F "$message" >/dev/null || { cat "$name.$language.log"; exit 1; }
    done
}
reject duplicate 'Only one surface light block' 'light {} light {}'
for block in "normal { bumps 0.5 max_trace_level 2 }:a normal" "pigment { bozo fallback rgb 1 }:a pigment" \
             "texture { checker texture { pigment { rgb 1 } } texture { pigment { rgb 0 } } camera {} }:a texture"; do
    printf '%s\n' '#version 3.7;' "sphere { 0,1 ${block%%:*} }" >nested.pov
    if "$POVRAY" +inested.pov +w1 +h1 +wt1 +pr -f -d -p -v -gp >nested.log 2>&1; then
        echo "surface_light: expected the nested item in ${block%%:*} to fail" >&2; exit 1
    fi
    tr '\n' ' ' <nested.log | tr -s '[:space:]' ' ' | grep -F "belongs inside the screen block of ${block#*:}." >/dev/null ||
        { cat nested.log; exit 1; }
done
reject input 'one pigment or emission input' 'light { pigment { rgb 1 } emission }'
reject brightness 'brightness must be finite and not negative' 'light { brightness -1 }'
reject samples 'Expected at least 1' 'light { samples 0 }'
reject zero_passes 'Bounce passes must be a positive integer' 'light { bounce { passes 0 } }'
reject negative_passes 'Bounce passes must be a positive integer' 'light { bounce { passes -1 } }'
reject fractional_passes 'Bounce passes must be a positive integer' 'light { bounce { passes 1.5 } }'
reject overflow_passes 'Bounce passes must be a positive integer' 'light { bounce { passes 4294967296 } }'
reject duplicate_bounce 'only one bounce setting' 'light { bounce bounce }'
reject duplicate_passes 'only one passes setting' 'light { bounce { passes 1 passes 3 } }'
reject bounce_aoi 'cannot depend on ray or camera context' 'pigment { aoi } light { bounce }'
reject angular_finish 'cannot bake direction-dependent finish attenuation' 'finish { emission 1 fresnel on } light { emission }'
reject bounce_crand 'cannot prepare stochastic crand diffuse finishes' 'finish { crand 0.5 } light { bounce }'
reject attenuation 'Surface light blocks do not accept attenuation' 'light { attenuation 2,1 }'
reject surface_photon_item "'reflection' belongs inside the photons block of" 'light { reflection on }'
reject volume_photon_item "'refraction' belongs inside the photons block of" 'hollow interior { light { refraction on } }'
reject object_photon_item "'target' belongs inside the photons block of" 'target'
reject finish_reflection_item "'falloff' belongs inside the reflection block of" 'finish { reflection 0.5 falloff 2 }'
reject media_scattering_item "'eccentricity' belongs inside the scattering block of" 'hollow interior { media { scattering { 1, 0.1 } eccentricity 0.3 } }'
reject zero_lumens 'requires positive surface emission' 'light { pigment { rgb 0 } brightness 1 lumens }'
reject negative_colour 'requires finite nonnegative emitted colour' 'light { pigment { rgb <-1,0,0> } }'
reject ray_context 'cannot depend on ray or camera context' 'light { pigment { aoi } }'
reject scale_lumens_metres 'scale_lumens takes finite lumens and a positive finite number of metres per unit' 'light { brightness scale_lumens(8, 0) lumens }'
for language in pov pov4; do
    echo 'sphere { 0,1 light { brightness scale_lumens(8, 2) lumens } }' >"scaled.$language"
    "$POVRAY" +i"scaled.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"scaled.$language.log" 2>&1 || { cat "scaled.$language.log"; exit 1; }
    if [ "$language" = pov ]; then
        { echo '#version 3.7;'; echo '#declare L = light_source { <0,2,-3> rgb 1 }'; echo 'object { L light {} }'; } >"lamp.$language"
    else
        { echo 'let L = light_source { <0,2,-3> rgb 1 };'; echo 'object { L light {} }'; } >"lamp.$language"
    fi
    if "$POVRAY" +i"lamp.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"lamp.$language.log" 2>&1; then
        echo "surface_light: light block on a light_source accepted in $language" >&2; exit 1
    fi
    tr '\n' ' ' <"lamp.$language.log" | tr -s '[:space:]' ' ' | grep -F 'a light_source cannot take one' >/dev/null || { cat "lamp.$language.log"; exit 1; }
done
{ echo 'let passes = 3;'; echo 'sphere { 0,1 light { bounce { passes passes - 1 } } }'; } >bound.pov4
"$POVRAY" +ibound.pov4 +w1 +h1 +wt1 +pr -f -d -p -v -gp >bound.log 2>&1 || { cat bound.log; exit 1; }
for language in pov pov4; do
    echo 'plane { y,0 light {} }' >"infinite.$language"
    if "$POVRAY" +i"infinite.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"infinite.$language.log" 2>&1; then
        echo "surface_light: infinite source accepted in $language" >&2; exit 1
    fi
    tr '\n' ' ' <"infinite.$language.log" | tr -s '[:space:]' ' ' | grep -F 'requires finite geometry with nonzero surface area' >/dev/null || { cat "infinite.$language.log"; exit 1; }
done
for language in pov pov4; do
    { if [ "$language" = pov ]; then echo '#version 3.7;'; fi; echo 'light_source { <0,2,-3> rgb 1 light {} }'; } >"direct.$language"
    if "$POVRAY" +i"direct.$language" +w1 +h1 +wt1 +pr -f -d -p -v -gp >"direct.$language.log" 2>&1; then
        echo "surface_light: light block inside a light_source accepted in $language" >&2; exit 1
    fi
    tr '\n' ' ' <"direct.$language.log" | tr -s '[:space:]' ' ' | grep -F 'a light_source cannot take one' >/dev/null || { cat "direct.$language.log"; exit 1; }
done
echo 'surface_light: ok'
