#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
SCENE="$SRCDIR/tests/render/progressive_photons4"
render() {
    name=$1; input=$2; shift 2
    "$POVRAY" +i"$input" +w32 +h24 +pr -a -d -p -v -gp +wt2 \
        +fp16 File_Gamma=1 +o"progressive_photons4_$name.ppm" "$@" \
        > "progressive_photons4_$name.log" 2>&1 || { cat "progressive_photons4_$name.log"; exit 1; }
    tail -c 4608 "progressive_photons4_$name.ppm" > "progressive_photons4_$name.px"
}
render classic "$SCENE.pov"
render native "$SCENE.pov4" +GLprogressive_photons4_lowered.pov
render lowered progressive_photons4_lowered.pov
cmp progressive_photons4_classic.px progressive_photons4_native.px
cmp progressive_photons4_classic.px progressive_photons4_lowered.px
od -An -v -t u1 progressive_photons4_native.px | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
for expression in 'quality' '+quality' '(quality * 2) / 2' 'quality ? 0.125 : 0.25' 'quality(0.0625)'; do
    sed "s|quality (quality \* 1)|quality $expression|" "$SCENE.pov4" > progressive_photons4_expression.pov4
    if [ "$expression" = 'quality(0.0625)' ]; then
        sed 's/let quality = 0.125;/fn quality(Q) { Q * 2 }/' progressive_photons4_expression.pov4 > progressive_photons4_function.pov4
        mv progressive_photons4_function.pov4 progressive_photons4_expression.pov4
    fi
    render expression progressive_photons4_expression.pov4
    cmp progressive_photons4_native.px progressive_photons4_expression.px
done
sed 's/quality (quality \* 1)/if (true) { quality (quality * 1) } else if (false) { quality +quality }/' \
    "$SCENE.pov4" > progressive_photons4_control.pov4
render control progressive_photons4_control.pov4
cmp progressive_photons4_native.px progressive_photons4_control.px
for language in pov pov4; do
    sed 's/method 2 quality (quality \* 1)/method 2/' "$SCENE.$language" > "progressive_photons4_default.$language"
    render "default_$language" "progressive_photons4_default.$language"
done
cmp progressive_photons4_default_pov.px progressive_photons4_default_pov4.px
sed 's/method 2 quality (quality \* 1)/quality (quality * 1) method 2/' "$SCENE.pov4" > progressive_photons4_order.pov4
render order progressive_photons4_order.pov4
cmp progressive_photons4_native.px progressive_photons4_order.px
for method in '' 'method 1'; do
    sed "s/method 2 quality (quality \* 1)/$method count 8192/" "$SCENE.pov4" > progressive_photons4_legacy.pov4
    render "legacy${method:+_explicit}" progressive_photons4_legacy.pov4
    sed "s/method 2 quality (quality \* 1)/$method count 8192/" "$SCENE.pov" > progressive_photons4_legacy.pov
    render legacy_classic progressive_photons4_legacy.pov
    cmp "progressive_photons4_legacy${method:+_explicit}.px" progressive_photons4_legacy_classic.px
done
cmp progressive_photons4_legacy.px progressive_photons4_legacy_explicit.px
fails_scene() {
    scene=$1; expected=$2
    printf '%s\n' "$scene" > progressive_photons4_invalid.pov4
    if "$POVRAY" +iprogressive_photons4_invalid.pov4 +w1 +h1 +pr -a -d -p -v -gp -f \
        > progressive_photons4_invalid.log 2>&1; then
        echo "invalid native photon scene accepted" >&2; exit 1
    fi
    tr -d '\n' < progressive_photons4_invalid.log | grep -Fq "$expected" || { cat progressive_photons4_invalid.log; exit 1; }
}
fails() {
    fails_scene "global_settings { photons { $1 } } sphere { 0, 1 }" "$2"
}
for quality in 0 -1 257 1e400; do
    fails "method 2 quality $quality" 'Photon quality must be finite'
done
fails 'method 3' 'Photon method must be 1'
fails 'method 1 quality 1' 'Photon quality requires method 2'
fails 'quality 1' 'Photon quality requires method 2'
for controls in 'count 100' 'spacing 0.1' 'gather 20, 100' 'radius 0.1' 'jitter 0.4' 'autostop 0.5' 'expand_thresholds 0.2, 35'; do
    fails "method 2 quality 1 $controls" 'Photon method 2 uses quality instead of'
done
fails 'method 2 quality "bad"' 'Photon quality must be a number'
fails 'method 2 quality ("bad")' 'Photon quality must be a number'
fails 'method 2 quality <1, 2, 3>' 'Photon quality must be a number'
fails 'method 2 quality(1, 2)' 'Photon quality takes one numeric expression'
fails 'method 2 if (false) { qualty 1 }' "did you mean 'quality'"
for parent in 'sphere { 0, 1' 'light_source { <0, 5, 0> rgb 1'; do
    for controls in 'method 1' 'method 2' 'quality 1' 'quality (1)' 'if (false) { method 2 quality 1 }'; do
        fails_scene "$parent photons { $controls } }" 'only valid inside global_settings'
    done
    fails_scene "fn Controls() { method 2 } $parent photons { Controls() } }" 'only valid inside global_settings'
    fails_scene "let Settings = photons { method 2 quality 1 }; $parent Settings }" 'method'
done
sed 's/photons { method 2 quality (quality \* 1) }/Settings/' "$SCENE.pov4" > progressive_photons4_fragment.pov4
printf '%s\n' 'let Settings = photons { method 2 quality 0.125 };' > progressive_photons4_bound.pov4
cat progressive_photons4_fragment.pov4 >> progressive_photons4_bound.pov4
render bound progressive_photons4_bound.pov4
cmp progressive_photons4_native.px progressive_photons4_bound.px
rm -f progressive_photons4_*.ppm progressive_photons4_*.px progressive_photons4_*.pov \
    progressive_photons4_*.pov4 progressive_photons4_*.pov-state progressive_photons4_*.log
echo 'progressive_photons4: native and lowered pixels, expressions, classic compatibility and failures passed'
