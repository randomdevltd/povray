#!/bin/sh
# render_tags.sh <povray> <srcdir>: decoded pixels and parse-time failures.
set -eu
POVRAY=$1
SRCDIR=$2

render() {
    render_name=$1
    render_scene=$2
    shift 2
    "$POVRAY" "$@" +i"$SRCDIR/$render_scene" +w64 +h32 -a -d -p -v -gp +pr +wt1 +fp16 \
        File_Gamma=1 +o"render_tags_$render_name.ppm" > "render_tags_$render_name.log" 2>&1 || {
        cat "render_tags_$render_name.log"
        exit 1
    }
    tail -c 12288 "render_tags_$render_name.ppm" > "render_tags_$render_name.px"
}

equal() {
    cmp "render_tags_$1.px" "render_tags_$2.px" || {
        echo "render tags mismatch: $1 / $2" >&2
        exit 1
    }
}

truth=tests/render/render_tags_truth.pov
for method in 1 2; do
    expr=0
    for mask in 15 14 1 10 8 14 14 1 2 0 15 4 2 12 0 14; do
        render "m${method}_expr${expr}" "$truth" +bm"$method" +mb1 Declare=Expr="$expr"
        render "m${method}_ref${expr}" "$truth" +bm"$method" +mb1 Declare=Expected="$mask"
        equal "m${method}_expr${expr}" "m${method}_ref${expr}"
        expr=$((expr + 1))
    done
done
for expr in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    equal "m1_expr${expr}" "m2_expr${expr}"
done
render late "$truth" Declare=Late=1
equal late m1_expr3
render cli "$truth" 'Filter_Tags="a*" & !"b" | "camera"'
equal cli m1_expr8
render universe "$truth" Declare=Expr=1 'Filter_Tags="a*" & !"b" | "camera"'
equal universe m1_expr8
render precedence "$truth" 'Filter_Tags="a" | "b" & !"a" | "camera"'
equal precedence m1_expr6
render escapes_sdl tests/render/render_tags_escapes.pov
render escapes_ref tests/render/render_tags_escapes.pov Declare=Reference=1
equal escapes_sdl escapes_ref
IFS= read -r escaped_filter < "$SRCDIR/tests/render/render_tags_escapes.ini"
render escapes_cli tests/render/render_tags_escapes.pov Declare=UseSDL=0 "$escaped_filter"
render escapes_ini tests/render/render_tags_escapes.pov Declare=UseSDL=0 "$SRCDIR/tests/render/render_tags_escapes.ini" +GIrendertags_generated.ini
render escapes_roundtrip tests/render/render_tags_escapes.pov rendertags_generated.ini
equal escapes_cli escapes_ref
equal escapes_ini escapes_ref
equal escapes_roundtrip escapes_ref
render legacy tests/render/render_tags_legacy.pov
render legacy_any_operator "$truth" Declare=LegacySymbols=1 Declare=Expr=1
render legacy_none_operator "$truth" Declare=LegacySymbols=1 Declare=Expr=2
equal legacy_any_operator m1_expr1
equal legacy_none_operator m1_expr2
for check in 1 2; do
    if "$POVRAY" +i"$SRCDIR/tests/render/render_tags_legacy.pov" +w8 +h8 -d -p -f \
        Declare=Check="$check" > "render_tags_legacy_$check.log" 2>&1; then
        echo "4.0 tag syntax accepted in legacy scene: $check" >&2
        exit 1
    fi
    grep -Eiq 'parse error|fatal error' "render_tags_legacy_$check.log"
done

for check in 0 1 2 3 4 5 6 7 8 9; do
    if "$POVRAY" +i"$SRCDIR/tests/render/render_tags_invalid.pov" +w8 +h8 -a -d -p -v -gp -f \
        Declare=Check="$check" > "render_tags_invalid_$check.log" 2>&1; then
        echo "invalid render tag syntax accepted: $check" >&2
        exit 1
    fi
    if ! grep -Eiq 'parse error|fatal error' "render_tags_invalid_$check.log"; then
        cat "render_tags_invalid_$check.log"
        exit 1
    fi
done
if "$POVRAY" +i"$SRCDIR/$truth" +w8 +h8 -d -p -f 'Filter_Tags="a" &' \
    > render_tags_invalid_cli.log 2>&1; then
    echo "invalid Filter_Tags accepted" >&2
    exit 1
fi

render macro tests/render/render_tags_macro.pov 'Filter_Tags="keep" | none'
render macro_ref tests/render/render_tags_macro.pov Declare=Reference=1
equal macro macro_ref

for method in 1 2; do
    for split in 0 1; do
        render "root_m${method}_s${split}" tests/render/render_tags_roots.pov +bm"$method" +mb1 Declare=Split="$split"
        render "root_ref_m${method}_s${split}" tests/render/render_tags_roots.pov +bm"$method" +mb1 Declare=Split="$split" Declare=Reference=1
        equal "root_m${method}_s${split}" "root_ref_m${method}_s${split}"
    done
done
equal root_m1_s0 root_m2_s1

for scene in screen portal lights; do
    render "${scene}_m1" "tests/render/render_tags_${scene}.pov" +bm1 +mb1
    render "${scene}_m2" "tests/render/render_tags_${scene}.pov" +bm2 +mb1
    equal "${scene}_m1" "${scene}_m2"
    if cmp -s render_tags_m1_expr9.px "render_tags_${scene}_m1.px"; then
        echo "render tags $scene rendered empty" >&2
        exit 1
    fi
done

for mode in 1 2 3 4; do
    render "portal_mode$mode" tests/render/render_tags_portal.pov Declare=Mode="$mode"
    equal portal_m1 "portal_mode$mode"
done
render portal_reject tests/render/render_tags_portal.pov Declare=Mode=5
equal portal_reject m1_expr9
for mode in 1 2; do
    render "screen_mode$mode" tests/render/render_tags_screen.pov Declare=Mode="$mode"
    equal screen_m1 "screen_mode$mode"
done
render screen_default tests/render/render_tags_default_camera.pov
render screen_default_ref tests/render/render_tags_default_camera.pov Declare=Reference=1
equal screen_default screen_default_ref
if cmp -s render_tags_screen_default.px render_tags_m1_expr9.px; then
    echo "default screen camera rendered empty" >&2
    exit 1
fi
for fallback in 0 1; do
    render "camera$fallback" tests/render/render_tags_cameras.pov Declare=Fallback="$fallback" 'Filter_Tags="keep" | none'
    render "camera_ref$fallback" tests/render/render_tags_cameras.pov Declare=Fallback="$fallback" Declare=Reference=1
    equal "camera$fallback" "camera_ref$fallback"
done
grep -q 'No camera survived Filter_Tags' render_tags_camera1.log
for effect in 0 1 2; do
    render "transport$effect" tests/render/render_tags_transport.pov Declare=Effect="$effect" +ss1
    render "transport_ref$effect" tests/render/render_tags_transport.pov Declare=Effect="$effect" Declare=Reference=1 +ss1
    equal "transport$effect" "transport_ref$effect"
done
render radio_save tests/render/render_tags_transport.pov Declare=Effect=1 +ss1 +RFO +RFrender_tags_radiosity.rca
set -- render_tags_radiosity.rca.set-*
test -f "$1" || { echo "missing per-set radiosity cache" >&2; exit 1; }
render radio_load tests/render/render_tags_transport.pov Declare=Effect=1 +ss1 +RFI +RFrender_tags_radiosity.rca
render radio_load_again tests/render/render_tags_transport.pov Declare=Effect=1 +ss1 +RFI +RFrender_tags_radiosity.rca
grep -Eq 'Loaded [1-9][0-9]* radiosity samples' render_tags_radio_load.log
equal radio_load radio_load_again
for variant in 0 1 2 3 4; do
    render "screen_radio$variant" tests/render/render_tags_screen_radiosity.pov \
        Declare=Variant="$variant" 'Filter_Tags="keep" | none' +ss1 +HR
done
equal screen_radio0 screen_radio1
equal screen_radio0 screen_radio2
equal screen_radio3 screen_radio4
if cmp -s render_tags_screen_radio0.px render_tags_screen_radio4.px; then
    echo "retained shared screen pigment did not affect radiosity fixture" >&2
    exit 1
fi

render photons_save tests/render/render_tags_photon_maps.pov +ss1
render photons_load tests/render/render_tags_photon_maps.pov Declare=LoadMap=1 +ss1
equal photons_save photons_load
if cmp -s render_tags_photons_save.px render_tags_m1_expr9.px; then
    echo "multi-set photon fixture rendered empty" >&2
    exit 1
fi
if "$POVRAY" +i"$SRCDIR/tests/render/render_tags_photon_maps.pov" +w8 +h8 -a -d -p -f \
    Declare=LoadMap=1 Declare=Mismatch=1 > render_tags_photon_mismatch.log 2>&1; then
    echo "mismatched prepared-set photon map accepted" >&2
    exit 1
fi
grep -qi 'Could not load photon map' render_tags_photon_mismatch.log
render photons_legacy_save tests/render/render_tags_transport.pov Declare=Effect=2 Declare=Reference=1 Declare=MapMode=1 +ss1
render photons_legacy_load tests/render/render_tags_transport.pov Declare=Effect=2 Declare=Reference=1 Declare=MapMode=2 +ss1
equal photons_legacy_save photons_legacy_load
if "$POVRAY" +i"$SRCDIR/tests/render/render_tags_transport.pov" +w8 +h8 -a -d -p -f \
    Declare=Effect=2 Declare=MapMode=2 > render_tags_photon_legacy_reject.log 2>&1; then
    echo "legacy photon map accepted in filtered scene" >&2
    exit 1
fi
grep -qi 'legacy photon map cannot be loaded' render_tags_photon_legacy_reject.log
render portal_photons tests/render/render_tags_portal_photons.pov +ss1
render portal_photons_dark tests/render/render_tags_portal_photons.pov Declare=Lamp=0 +ss1
equal portal_photons_dark m1_expr9
if cmp -s render_tags_portal_photons.px render_tags_portal_photons_dark.px; then
    echo "portal photons did not reach destination prepared set" >&2
    exit 1
fi
render photon_light_group tests/render/render_tags_photon_light_group.pov +ss1
render photon_light_group_ref tests/render/render_tags_photon_light_group.pov Declare=Reference=1 +ss1
equal photon_light_group photon_light_group_ref
if cmp -s render_tags_photon_light_group.px render_tags_m1_expr9.px; then
    echo "light-group photon receiver rendered empty" >&2
    exit 1
fi

render parallel tests/render/render_tags_parallel.pov Declare=Mode=0
render parallel_a tests/render/render_tags_parallel.pov Declare=Mode=2
render parallel_b tests/render/render_tags_parallel.pov Declare=Mode=3
for world in parallel parallel_a parallel_b; do
    od -An -v -tx1 -w6 "render_tags_$world.px" | tr -d ' ' > "render_tags_$world.hex"
done
# Each pixel of two worlds sharing coordinates must be world a's or, through the portal, world b's.
paste -d' ' render_tags_parallel.hex render_tags_parallel_a.hex render_tags_parallel_b.hex | awk '
    $1 == $2 { a++; next }
    $1 == $3 { b++; next }
    { other++ }
    END { if (other || a < 200 || b < 200) { printf "parallel worlds: %d from a, %d from b, %d from neither\n", a, b, other > "/dev/stderr"; exit 1 } }'

rm -f render_tags_*.ppm render_tags_*.px render_tags_*.hex render_tags_*.log render_tags_photon_maps.ph render_tags_legacy.ph render_tags_radiosity.rca.set-* rendertags_generated.ini
echo "render_tags: expressions, macros, CSG, lights, screens and portals passed"
