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
render alias "$scene" Declare=Case=5 Declare=Alias=1 Declare=Units=2
render pigment5 "$scene" Declare=Case=5 Declare=Units=2
cmp pigment5.rgb alias.rgb
render unitless "$scene" Declare=Units=2
cmp geometry0.rgb unitless.rgb
render native_alias "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=5 Declare=Alias=1 Declare=Units=2
cmp alias.rgb native_alias.rgb
# nits: the emission alias's and a raw pigment's mean radiance over the surface is the stated nits, as a unitless
# pigment divided by that mean by hand
render alias_nits "$scene" Declare=Case=10 Declare=Alias=1
render native_alias_nits "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=10 Declare=Alias=1
cmp alias_nits.rgb native_alias_nits.rgb
render pattern_nits "$scene" Declare=Case=10 Declare=Raw=1
render native_pattern_nits "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=10 Declare=Raw=1
cmp pattern_nits.rgb native_pattern_nits.rgb
render pattern_hand "$scene" Declare=Case=10 Declare=Units=2
render pattern_lumens "$scene" Declare=Case=10 Declare=Raw=1 Declare=Units=1
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
# a bounce-only block's brightness multiplies the light it re-emits
for gain in 0 1 2; do
    render "gain$gain" "$scene" Declare=Case=8 Declare=Bounce=2 Declare=Passive=1 Declare=Volume=1 Declare=Gain="$gain"
done
render native_gain2 "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=8 Declare=Bounce=2 Declare=Passive=1 Declare=Volume=1 Declare=Gain=2
cmp gain2.rgb native_gain2.rgb
render portal "$scene" Declare=Case=9
render native_portal "$SRCDIR/tests/render/surface_light.pov4" Declare=Case=9
cmp portal.rgb native_portal.rgb
# a cylinder, a lathe of it in a union, and it in a union whose other part is tiny and far away light the floor alike
for held in 11 12 13; do
    render "held$held" "$scene" Declare=Case="$held" Declare=Samples=128
done
{ echo '#version 3.7;'
  echo 'global_settings { assumed_gamma 1 }'
  echo 'camera { orthographic location <0,0,-5> look_at 0 right x*2.4 up y*2.4 }'
  echo 'sphere { 0,1 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } double_illuminate'
  echo '  light { pigment { rgb 1 } brightness 4 samples 64 } }'
} >self.pov
render self self.pov
. "$SRCDIR/tests/render/image.sh"
image_awk '
function power(name,    v) { return sum_of(v, read_words(name ".rgb", v, 192)) }
function check(ok, message) { if (!ok) fail("surface_light: " message) }
BEGIN {
    for (c = 0; c < 8; c++) check(power("geometry" c) > 0, "geometry" c " is black")
    check(power("native7") > 0, "native7 is black")
    check(abs(power("native7") / power("geometry7") - 1) < 0.05, "native7 power " power("native7") ", geometry7 " power("geometry7") ", limit 5%")
    check(power("passive") == 0, "passive emits " power("passive"))
    check(power("csg") > 0, "csg is black")
    check(power("portal") > 0, "portal is black")
    check(power("self") == 0, "self emits " power("self"))
    reference = power("geometry0")
    check(abs(power("lumens") / reference - 1) < 0.12, "lumens power " power("lumens") ", geometry0 " reference ", limit 12%")
    check(abs(power("alias_nits") / power("pattern_hand") - 1) < 0.005, "alias nits power " power("alias_nits") ", hand-normalized pigment " power("pattern_hand") ", limit 0.5%")
    check(abs(power("pattern_nits") / power("pattern_hand") - 1) < 0.005, "pigment nits power " power("pattern_nits") ", hand-normalized pigment " power("pattern_hand") ", limit 0.5%")
    check(abs(power("pattern_lumens") / power("pattern_nits") - 1) < 0.005, "pigment lumens power " power("pattern_lumens") ", the same flux in nits " power("pattern_nits") ", limit 0.5%")
    bounce1 = power("gain1") - power("gain0"); bounce2 = power("gain2") - power("gain0")
    check(bounce1 > 0, "a bounce gain of 1 adds no light over 0")
    check(abs(bounce2 / (2 * bounce1) - 1) < 0.01, "bounce gain 2 adds " bounce2 ", twice gain 1 is " 2 * bounce1 ", limit 1%")
    for (held = 12; held <= 13; held++)
        check(abs(power("held" held) / power("held11") - 1) < 0.02, "held" held " power " power("held" held) ", cylinder " power("held11") ", limit 2%")
    check(abs(power("few") / power("many") - 1) < 0.35, "few-sample power " power("few") ", many " power("many") ", limit 35%")
}'
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
reject surface_power 'attenuation accepts only cutoff; power and scale are for interior lights' 'light { attenuation { power 2 } }'
reject surface_scale 'attenuation accepts only cutoff; power and scale are for interior lights' 'light { attenuation { cutoff 0 scale 1 } }'
reject surface_attenuation_number 'attenuation is a block: attenuation { cutoff 0.000004 }' 'light { attenuation 1 }'
reject negative_cutoff 'cutoff must be finite and not negative' 'light { attenuation { cutoff -1 } }'
reject volume_negative_cutoff 'cutoff must be finite and not negative' 'hollow interior { light { attenuation { cutoff -0.5 } } }'
reject duplicate_cutoff 'accepts only one cutoff' 'light { attenuation { cutoff 0 cutoff 1 } }'
reject volume_duplicate_cutoff 'accepts only one cutoff' 'hollow interior { light { attenuation { cutoff 0 power 2 cutoff 0 } } }'
reject attenuation_pair 'attenuation is a block: attenuation { power 2 scale 1 }' 'hollow interior { light { attenuation 2, 1 } }'
reject attenuation_scale 'attenuation is a block: attenuation { power 2 scale 1 }' 'hollow interior { light { attenuation 1 } }'
reject duplicate_power 'accepts only one power' 'hollow interior { light { attenuation { power 2 power 1 } } }'
reject duplicate_scale 'accepts only one scale' 'hollow interior { light { attenuation { scale 1 scale 2 } } }'
reject duplicate_attenuation 'only one attenuation block' 'hollow interior { light { attenuation { } attenuation { } } }'
reject attenuation_power 'takes a power greater than 0 and a scale of 0 or more' 'hollow interior { light { attenuation { power 0 } } }'
reject attenuation_negative 'takes a power greater than 0 and a scale of 0 or more' 'hollow interior { light { attenuation { scale -1 } } }'
reject surface_photon_item "'reflection' belongs inside the photons block of" 'light { reflection on }'
reject volume_photon_item "'refraction' belongs inside the photons block of" 'hollow interior { light { refraction on } }'
reject object_photon_item "'target' belongs inside the photons block of" 'target'
reject finish_reflection_item "'falloff' belongs inside the reflection block of" 'finish { reflection 0.5 falloff 2 }'
reject media_scattering_item "'eccentricity' belongs inside the scattering block of" 'hollow interior { media { scattering { 1, 0.1 } eccentricity 0.3 } }'
reject zero_lumens 'A light stated in nits or lumens requires an input of positive luminance' 'light { pigment { rgb 0 } brightness 1 lumens }'
reject zero_pigment_nits 'A light stated in nits or lumens requires an input of positive luminance' 'light { pigment { rgb 0 } brightness 1 nits }'
reject zero_nits 'A surface light stated in nits requires an input of positive luminance' 'finish { emission 0 } light { emission brightness 1 nits }'
reject zero_pattern_nits 'A surface light stated in nits requires an input of positive luminance' 'light { pigment { bozo color_map { [0 rgb 0] [1 rgb 0] } } brightness 1 nits }'
reject zero_pattern_lumens 'A surface light stated in lumens requires an input of positive luminance' 'light { pigment { bozo color_map { [0 rgb 0] [1 rgb 0] } } brightness 1 lumens }'
bounce_only='A bounce-only light block takes a plain brightness multiplier; nits and lumens need an emission input.'
reject bounce_nits "$bounce_only" 'light { brightness 2 nits bounce }'
reject bounce_lumens "$bounce_only" 'light { bounce { passes 2 } brightness 10 lumens }'
reject volume_bounce_nits "$bounce_only" 'hollow interior { light { bounce brightness 2 nits } }'
reject volume_bounce_lumens "$bounce_only" 'hollow interior { light { brightness 10 lumens bounce { passes 1 } } }'
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
for language in pov pov4; do
    for kind in surface volume; do
        for cutoff in implicit default zero huge; do
            case $cutoff in
                implicit) att= ;;
                default) att='attenuation { cutoff 0.000004 }' ;;
                zero) att='attenuation { cutoff 0 }' ;;
                huge) att='attenuation { cutoff 1000 }' ;;
            esac
            if [ "$kind" = surface ]; then light="sphere { <0,1,0>, 0.2 no_image light { brightness 4 samples 4 $att } }"
            else light="sphere { <0,1,0>, 0.2 no_image hollow pigment { rgbt 1 } interior { light { brightness 4 samples 4 $att } } }"; fi
            { if [ "$language" = pov ]; then echo '#version 3.7;'; fi
              echo 'global_settings { assumed_gamma 1 }'
              echo 'camera { orthographic location <0,3,0> look_at 0 right x*4 up z*4 }'
              echo 'plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }'
              echo "$light"
            } >"cut_$kind$cutoff.$language"
            render "cut_$kind${cutoff}_$language" "cut_$kind$cutoff.$language"
        done
        cmp "cut_${kind}implicit_$language.rgb" "cut_${kind}default_$language.rgb"
        cmp "cut_${kind}implicit_$language.rgb" "cut_${kind}implicit_pov.rgb"
    done
done
image_awk '
function power(name,    v) { return sum_of(v, read_words(name ".rgb", v, 192)) }
function check(ok, message) { if (!ok) fail("surface_light: " message) }
BEGIN {
    split("surface volume", kinds, " ")
    for (k = 1; k <= 2; k++) {
        kind = kinds[k]
        check(power("cut_" kind "zero_pov") > 0, kind " light with cutoff 0 is black")
        check(abs(power("cut_" kind "implicit_pov") / power("cut_" kind "zero_pov") - 1) < 0.001, kind " default cutoff changes the image")
        check(power("cut_" kind "huge_pov") == 0, kind " light above its cutoff still lights " power("cut_" kind "huge_pov"))
    }
}'
echo 'surface_light: ok'
