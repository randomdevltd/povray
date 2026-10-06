#!/bin/sh
# render.sh <povray> <srcdir> <outdir> [image...]: the portal and screen images of the manual, from the scenes here and in tests/render.
POVRAY=$1; SRCDIR=$2; OUT=$3; shift 3
ONLY=" $* "
DOC=$SRCDIR/doc/portals; FIX=$SRCDIR/tests/render
mkdir -p "$OUT"
render() {
    name=$1; scene=$2; shift 2
    case "$ONLY" in "  "|*" $name "*) ;; *) return 0 ;; esac
    echo "== $name"
    "$POVRAY" +i"$scene" +L"$DOC" +L"$FIX" +L"$SRCDIR/distribution/include" +w320 +h240 +a0.1 +am2 +r3 +tf -j +ss1 +wt2 \
        -d -p -v +fn +o"$OUT/$name.png" "$@" || FAILED="$FAILED $name"
}

render portal_o_intro "$DOC/portal_o_intro.pov"

render portal_r_screen_window "$DOC/portal_r_screen_window.pov" +ua
render portal_r_screen_fallback "$DOC/portal_r_screen_fallback.pov"
render portal_r_screen_depth "$DOC/portal_r_screen_depth.pov"
render portal_r_screen_radiosity "$DOC/portal_r_screen_radiosity.pov"
render portal_r_sides "$DOC/portal_r_sides.pov"
render portal_r_far "$DOC/portal_r_far.pov"
render portal_r_near_far "$DOC/portal_r_near_far.pov"
render portal_r_pigment "$DOC/portal_r_pigment.pov"
render portal_r_perturb "$DOC/portal_r_perturb.pov"
render portal_r_exit "$DOC/portal_r_exit.pov"
render portal_r_depth "$DOC/portal_r_depth.pov"
render portal_r_volume "$DOC/portal_r_volume.pov"
render portal_r_lights "$DOC/portal_r_lights.pov"
render portal_r_lights_off "$DOC/portal_r_lights.pov" Declare=NoLights=1
render portal_r_shadows "$DOC/portal_r_shadows.pov"

render portal_w01_television "$DOC/portal_w01_television.pov"
render portal_w02_security "$DOC/portal_w02_security.pov"
render portal_w03_feedback "$FIX/screen_feedback.pov" Declare=Depth=12
render portal_w04_book "$FIX/portal_book.pov"
render portal_w05_doorways "$DOC/portal_w05_doorways.pov"
render portal_w06_wormhole "$DOC/portal_w06_wormhole.pov"
render portal_w07_jar "$FIX/portal_belljar.pov"
render portal_w08_candle "$DOC/portal_w05_doorways.pov" Declare=Night=1
render portal_w09_halftone "$DOC/portal_w09_halftone.pov"
render portal_w10_projection_gallery "$DOC/portal_w10_projection_gallery.pov"
render portal_w11_world_map "$DOC/portal_w11_world_map.pov"

if [ -n "$FAILED" ]; then echo "failed:$FAILED"; exit 1; fi
