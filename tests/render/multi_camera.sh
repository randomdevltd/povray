#!/bin/sh
# multi_camera.sh <povray> <srcdir>: every camera with an output block matches a render of that camera alone, pixel for pixel.
set -eu
trap 'rm -f multi_camera_*.ppm multi_camera_*.px multi_camera_*.log multi_camera_*.pov-state* multi_camera_*.ph' EXIT
POVRAY=$1
SRCDIR=$2
scene=$SRCDIR/tests/render/multi_camera.pov

size="+w64 +h48"
run() {
    run_name=$1
    run_scene=$2
    shift 2
    rm -f "multi_camera_$run_name.ppm" "multi_camera_$run_name"-[0-9]*.ppm
    "$POVRAY" +i"$run_scene" $size -a -d -p -v -gp +wt2 +fp16 File_Gamma=1 "$@" > "multi_camera_$run_name.log" 2>&1 || {
        cat "multi_camera_$run_name.log"
        exit 1
    }
}

# the pixels of a 16-bit image of $2 pixels (64x48 by default), without the header comment naming the file
pixels() {
    tail -c $((6 * ${2:-3072})) "multi_camera_$1.ppm" > "multi_camera_$1.px"
}

same() {
    pixels "$1" "${3:-3072}"
    pixels "$2" "${3:-3072}"
    cmp "multi_camera_$1.px" "multi_camera_$2.px" || {
        echo "multi camera mismatch: $1 / $2" >&2
        exit 1
    }
}

# a log's text with spaces and line breaks removed, as the console wraps long lines
logged() {
    tr -d ' \r\n' < "multi_camera_$1.log" | grep -qF "$2"
}

fail() {
    echo "multi camera: $*" >&2
    exit 1
}

refused() {
    refused_name=$1
    refused_text=$2
    refused_scene=$3
    shift 3
    if "$POVRAY" +i"$refused_scene" +w16 +h16 -d -p -gp +fp16 +o"multi_camera_$refused_name.ppm" "$@" > "multi_camera_$refused_name.log" 2>&1; then
        echo "multi camera: $refused_name was accepted" >&2
        exit 1
    fi
    logged "$refused_name" "$(printf '%s' "$refused_text" | tr -d ' ')" || {
        cat "multi_camera_$refused_name.log"
        exit 1
    }
}

for feature in 0 1 2 3 4 5 8; do
    run "f$feature" "$scene" Declare=Feature=$feature +o"multi_camera_f$feature.ppm"
    for cam in 0 1 2; do
        run "f${feature}_alone$cam" "$scene" Declare=Feature=$feature Declare=Cam=$cam +o"multi_camera_f${feature}_alone$cam.ppm"
        same "f$feature-$((cam + 1))" "f${feature}_alone$cam"
    done
done

# cameras without filter_tags share one prepared set, whose photon maps are shot once and reused
for feature in 2 3 6 7 8 9; do
    run "shared$feature" "$scene" Declare=Shared=1 Declare=Feature=$feature +o"multi_camera_shared$feature.ppm"
    for cam in 0 1 2; do
        run "shared${feature}_alone$cam" "$scene" Declare=Shared=1 Declare=Feature=$feature Declare=Cam=$cam \
            +o"multi_camera_shared${feature}_alone$cam.ppm"
        same "shared$feature-$((cam + 1))" "shared${feature}_alone$cam"
    done
done

# output blocks at older scene versions
legacy=$SRCDIR/tests/render/multi_camera_legacy.pov
for version in 3.1 3.7; do
    run "legacy$version" "$legacy" Declare=Version=$version +o"multi_camera_legacy$version.ppm"
    run "legacy${version}_alone0" "$legacy" Declare=Version=$version Declare=Cam=0 +w40 +h30 +o"multi_camera_legacy${version}_alone0.ppm"
    run "legacy${version}_alone1" "$legacy" Declare=Version=$version Declare=Cam=1 +w30 +h40 +o"multi_camera_legacy${version}_alone1.ppm"
    same "legacy$version-1" "legacy${version}_alone0" 1200
    same "legacy$version-2" "legacy${version}_alone1" 1200
done

# cameras of their own size, window and file; a render of one alone sets them on the command line
output=$SRCDIR/tests/render/multi_camera_output.pov
run sized "$output" +o"multi_camera_sized.ppm"
run sized_alone0 "$output" Declare=Cam=0 +w48 +h64 +o"multi_camera_sized_alone0.ppm"
run sized_alone1 "$output" Declare=Cam=1 +w32 +h16 +sc0.25 +sr0 +ec1 +er1 +o"multi_camera_sized_alone1.ppm"
run sized_alone2 "$output" Declare=Cam=2 +sc9 +sr5 +ec40 +er30 +o"multi_camera_sized_alone2.ppm"
same sized-1 sized_alone0
same thumb sized_alone1 512
same sized-3 sized_alone2
aspect=$SRCDIR/tests/render/multi_camera_aspect.pov
run aspect "$aspect" +o"multi_camera_aspect.ppm"
for case in 0:48:24 1:32:32 2:24:48 3:40:20 4:48:24 5:24:32; do
    cam=${case%%:*}; dims=${case#*:}; width=${dims%:*}; height=${dims#*:}
    run "aspect_alone$cam" "$aspect" Declare=Cam=$cam +w$width +h$height +o"multi_camera_aspect_alone$cam.ppm"
    same "aspect-$((cam + 1))" "aspect_alone$cam" $((width * height))
done
# every camera of its own size: the command line sets no size at all
size=""
run nosize "$aspect" +o"multi_camera_nosize.ppm"
size="+w64 +h48"
for case in 0:48:24 1:32:32 2:24:48 3:40:20 4:48:24 5:24:32; do
    cam=${case%%:*}; dims=${case#*:}
    same "nosize-$((cam + 1))" "aspect_alone$cam" $((${dims%:*} * ${dims#*:}))
done

four=$SRCDIR/tests/render/multi_camera_output.pov4
run four "$four" +o"multi_camera_four.ppm"
run four_alone0 "$four" Declare=Cam=0 +w48 +h64 +o"multi_camera_four_alone0.ppm"
run four_alone1 "$four" Declare=Cam=1 +w32 +h16 +o"multi_camera_four_alone1.ppm"
same four-1 four_alone0
same four four_alone1 512
run four_alone2 "$four" Declare=Cam=2 +o"multi_camera_four_alone2.ppm"
same four-3 four_alone2

run bound "$SRCDIR/tests/render/multi_camera_bound.pov4" +o"multi_camera_bound.ppm"
[ -s multi_camera_bound.ppm ] || fail "a bound output name made no image"
if [ -e multi_camera_bound-1.ppm ]; then fail "a bound output name was taken for the camera item"; fi

# a screen's camera with an output item also renders as its own view
screen=$SRCDIR/tests/render/multi_camera_screen.pov
run screen "$screen" +o"multi_camera_screen.ppm"
for cam in 0 1; do
    run "screen_alone$cam" "$screen" Declare=Cam=$cam +o"multi_camera_screen_alone$cam.ppm"
    same "screen-$((cam + 1))" "screen_alone$cam"
done
# with radiosity the screen's view seeing its own screen still finishes
run screen_radiosity "$screen" Declare=Radiosity=1 +o"multi_camera_screen_radiosity.ppm"
# an inherited output makes no screen view, a repeated screen is one view, and Filter_Tags can drop a screen's view
rm -f multi_camera_view.ppm
run inherited "$screen" Declare=Case=1 +o"multi_camera_inherited.ppm"
logged inherited "Renderingcamera1of1,64by48,to" && [ -s multi_camera_view.ppm ] || fail "a screen inherited a declared camera's output"
run repeated "$screen" Declare=Case=2 +o"multi_camera_repeated.ppm"
logged repeated "Renderingcamera2of2,64by48,to" || fail "a repeated screen gave more than one view"
run filtered "$screen" Declare=Case=3 'Filter_Tags="main" | none' +o"multi_camera_filtered.ppm"
logged filtered "Renderingcamera1of1,64by48,to" || fail "Filter_Tags kept a screen's view"

# a turntable of twelve cameras made in a loop, with method 2 photons shot once; frames are numbered 01 to 12
turntable=$SRCDIR/tests/render/multi_camera_turntable.pov
size="+w32 +h24"
run turntable "$turntable" Declare=Photons=2 +o"multi_camera_turntable.ppm"
for cam in 0 1 2 3 4 5 6 7 8 9 10 11; do
    run "turntable_alone$cam" "$turntable" Declare=Photons=2 Declare=Cam=$cam +o"multi_camera_turntable_alone$cam.ppm"
    same "turntable-$(printf '%02d' $((cam + 1)))" "turntable_alone$cam" 768
done
size="+w64 +h48"

mesh=$SRCDIR/tests/render/multi_camera_mesh.pov
run mesh_sized "$mesh" Declare=Sized=1 +o"multi_camera_mesh_sized.ppm"
run mesh_sized_alone0 "$mesh" Declare=Sized=1 Declare=Cam=0 +w40 +h40 +o"multi_camera_mesh_sized_alone0.ppm"
run mesh_sized_alone1 "$mesh" Declare=Sized=1 Declare=Cam=1 +w24 +h48 +o"multi_camera_mesh_sized_alone1.ppm"
same mesh_sized-1 mesh_sized_alone0 1600
same mesh_sized-2 mesh_sized_alone1 1152
for radiosity in 0 1; do
    run "mesh$radiosity" "$mesh" Declare=Radiosity=$radiosity +o"multi_camera_mesh$radiosity.ppm"
    for cam in 0 1; do
        run "mesh${radiosity}_alone$cam" "$mesh" Declare=Radiosity=$radiosity Declare=Cam=$cam +o"multi_camera_mesh${radiosity}_alone$cam.ppm"
        same "mesh$radiosity-$((cam + 1))" "mesh${radiosity}_alone$cam"
    done
done

# progressive renders, and a continued render skips every camera already written
run progressive "$scene" Declare=Feature=2 +pr +o"multi_camera_progressive.ppm"
for cam in 0 1 2; do
    run "progressive_alone$cam" "$scene" Declare=Feature=2 Declare=Cam=$cam +pr +o"multi_camera_progressive_alone$cam.ppm"
    same "progressive-$((cam + 1))" "progressive_alone$cam"
done
run continued "$scene" Declare=Feature=2 +pr +c +o"multi_camera_progressive.ppm"
if grep -q 'Rendering\.\.\.' multi_camera_continued.log || [ "$(grep -c '^Rendering camera' multi_camera_continued.log)" -ne 3 ]; then
    cat multi_camera_continued.log
    exit 1
fi

refused animation "cannot be combined with an animation" "$scene" +kff2
refused samefile "would write their images to the same file" "$output" Declare=Clash=1
refused clash "would write their images to the same file" "$output" Declare=Clash=2
"$POVRAY" +i"$output" +w64 +h48 -d -p -gp -f Declare=Clash=1 > multi_camera_nofile.log 2>&1 || {
    cat multi_camera_nofile.log
    exit 1
}
refused reversed "Invalid end column or row" "$output" Declare=BadWindow=2
refused empty "Invalid end column or row" "$output" Declare=BadWindow=3
refused photonfile "cannot be used with camera output blocks" "$scene" Declare=Feature=2 Declare=PhotonFile=1

# cameras without output blocks render as always: the last one
run plain "$scene" Declare=Plain=1 +o"multi_camera_plain.ppm"
same plain f0_alone2
logged plain "Morethanonecamera" || fail "several plain cameras gave no warning"
# a plain camera among output cameras is not rendered, and says so
run mixed "$output" Declare=Mixed=1 +o"multi_camera_mixed.ppm"
logged mixed "Thiscamerahasnooutputandisnotrendered" || fail "a plain camera among output cameras gave no warning"
# Filter_Tags picks one output camera, or leaves none and the scene renders its default camera as always
rm -f multi_camera_thumb.ppm
run picked "$output" 'Filter_Tags="view-thumb" | "context" | "detail"' +o"multi_camera_picked.ppm"
same thumb sized_alone1 512
if [ -e multi_camera_picked-1.ppm ] || [ -e multi_camera_picked.ppm ]; then fail "Filter_Tags rendered a dropped camera"; fi
run rejected "$output" 'Filter_Tags="context" | "detail"' +o"multi_camera_rejected.ppm"
[ -s multi_camera_rejected.ppm ] || fail "with no output camera left the scene rendered nothing"
logged rejected "Nocamerasurvived" || fail "no camera surviving was not reported"

# a camera that fails stops the render with an error, and the images before it stay
rm -f multi_camera_partway-1.ppm
if "$POVRAY" +i"$output" -d -p -gp +fp16 Declare=BadWindow=1 +o"multi_camera_partway.ppm" \
    > multi_camera_partway.log 2>&1; then
    echo "multi camera: a failing camera did not fail the render" >&2
    exit 1
fi
{ logged partway "Invalidendcolumnorrow" && [ -s multi_camera_partway-1.ppm ]; } || {
    cat multi_camera_partway.log
    exit 1
}
