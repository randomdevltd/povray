#!/bin/sh
# domain_cells.sh <povray> <srcdir>: the scene-wide, per-medium and per-light domain_cells parse, accept, reject and
# set an interior light's source lattice; each grid of an interior follows only its own count or detail.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
W=32; H=24
scene=$SRCDIR/tests/render/domain_cells.pov
render() {
    name=$1; shift
    "$POVRAY" +i"$scene" +L"$SRCDIR/distribution/include" +w$W +h$H +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
brightness() {
    tail -c $(( 6 * W * H )) "$1.ppm" | od -An -v -tu1 -w6 | awk '{ s += ($1 * 256 + $2) / 65535; n++ } END { printf "%.5f", s / n }'
}
# default, lowered and per-light-pinned budgets all light the floor through the media
render unset
render lowered Declare=Cells=65536
render pinned Declare=Cells=65536 Declare=LightCells=262144
for name in unset lowered pinned; do
    value=$(brightness $name)
    awk -v v="$value" 'BEGIN { exit !(v > 0.002) }' || { cat "$name.log"; echo "domain_cells: $name render is black"; exit 1; }
done
write() {
    { if [ "$2" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
      echo 'global_settings { assumed_gamma 1 }'
      printf '%s\n' "$3"
    } >"$1.$2"
}
# without a domain_cells of its own the light takes the scene's light default, the same lattice as stating it on the
# light, and never its medium's
lattice() {
    name=$1; shift
    "$POVRAY" +i"$scene" +L"$SRCDIR/distribution/include" +w$W +h$H +wt1 +pr -a -d -p -f Warning_Level=5 "$@" >"$name.log" 2>&1 ||
        { cat "$name.log" >&2; exit 1; }
    tr '\n' ' ' <"$name.log" | tr -s '[:space:]' ' ' | grep -o "Prepared interior light grid: [0-9]* x [0-9]* x [0-9]*" | head -1
}
stated=$(lattice stated Declare=LightCells=262144)
defaulted=$(lattice defaulted Declare=DefaultLightCells=262144)
medium=$(lattice medium Declare=MediumCells=262144)
fallback=$(lattice fallback)
[ "$stated" = "Prepared interior light grid: 65 x 65 x 65" ] || { cat stated.log; echo "domain_cells: the light's own count gave '$stated'"; exit 1; }
[ "$defaulted" = "$stated" ] || { cat defaulted.log; echo "domain_cells: the light default gave '$defaulted', not '$stated'"; exit 1; }
[ -n "$fallback" ] && [ "$fallback" != "$stated" ] || { cat fallback.log; echo "domain_cells: the scene default gave '$fallback'"; exit 1; }
[ "$medium" = "$fallback" ] || { cat medium.log; echo "domain_cells: the light took its medium's count: '$medium', not '$fallback'"; exit 1; }
# the scene-wide default and the medium item are accepted in the 4.0 language too, where the static checker knows them
write accepted pov4 'camera { location <0, 2, -4> look_at 0 right x up y }
default { media { domain_cells 65536 } }
default { interior { light { domain_cells 4096 } } light { domain_cells 64 } }
box { -1, 1 hollow pigment { rgbt 1 } interior { media { method 4 domain_cells 4096 absorption 0.5 } } }'
"$POVRAY" +iaccepted.pov4 +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >accepted.pov4.log 2>&1 || { cat accepted.pov4.log; exit 1; }
reject() {
    name=$1; value=$2
    for language in pov pov4; do
        for form in default lightdefault interiordefault medium; do
            if [ "$form" = default ]; then
                if [ "$language" = pov ]; then item="#default { media { domain_cells $value } }"; else item="default { media { domain_cells $value } }"; fi
            elif [ "$form" = lightdefault ]; then
                if [ "$language" = pov ]; then item="#default { light { domain_cells $value } }"; else item="default { light { domain_cells $value } }"; fi
            elif [ "$form" = interiordefault ]; then
                if [ "$language" = pov ]; then item="#default { interior { light { domain_cells $value } } }"; else item="default { interior { light { domain_cells $value } } }"; fi
            else
                item="box { -1, 1 hollow pigment { rgbt 1 } interior { media { method 4 domain_cells $value absorption 0.5 } } }"
            fi
            write invalid "$language" "$item"
            if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >"$name.$form.$language.log" 2>&1; then
                echo "domain_cells: expected $name in $form in $language to fail" >&2; exit 1
            fi
            tr '\n' ' ' <"$name.$form.$language.log" | tr -s '[:space:]' ' ' | grep -F "domain_cells must be an integer from 1 to 1000000000." >/dev/null ||
                { cat "$name.$form.$language.log"; exit 1; }
        done
    done
}
reject zero 0
reject negative -5
reject fractional 1.5
reject too_large 1000000001
write_light() {
    { if [ "$2" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
      echo 'global_settings { assumed_gamma 1 }'
      echo 'sphere { 0, 1 pigment { rgbt 1 } interior {'
      printf '  light { brightness 12 samples 4 %s }\n' "$3"
      echo '  media { method 4 absorption 0.5 } } }'
    } >"$1.$2"
}
light_case() {
    name=$1; value=${2-}; message=${3-}
    for language in pov pov4; do
        write_light valid "$language" 'domain_cells 262144'
        if [ "$name" = accepted ]; then
            "$POVRAY" +i"valid.$language" +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >"$name.$language.log" 2>&1 ||
                { cat "$name.$language.log"; exit 1; }
            continue
        fi
        write_light invalid "$language" "domain_cells $value"
        if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            echo "domain_cells: expected $name in $language to fail" >&2; exit 1
        fi
        tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ' | grep -F "$message" >/dev/null ||
            { cat "$name.$language.log"; exit 1; }
    done
}
light_case accepted
light_case light_zero 0 "domain_cells must be an integer from 1 to 1000000000."
light_case light_negative -5 "domain_cells must be an integer from 1 to 1000000000."
light_case light_fractional 1.5 "domain_cells must be an integer from 1 to 1000000000."
light_case light_too_large 1000000001 "domain_cells must be an integer from 1 to 1000000000."
light_case duplicate '262144 domain_cells 262144' "A light block accepts only one domain_cells setting."
# a declared light keeps its pin through a copy: the prepared grid reaches 65 cells per axis
cat >copied.pov <<'SCENE'
#version 3.8;
#default { media { domain_cells 65536 } }
global_settings { assumed_gamma 1 }
camera { location <0, 2, -4> look_at <0, 0.3, 0> right x up y }
background { rgb 0 }
plane { y, -1 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#declare Lamp = sphere {
  0, 0.6 hollow
  pigment { rgbt 1 }
  interior {
    light { brightness 12 samples 8 domain_cells 262144 }
    media { method 4 absorption 0.5 }
  }
}
object { Lamp translate -x*1.4 }
object { Lamp translate x*1.4 }
SCENE
"$POVRAY" +icopied.pov +L"$SRCDIR/distribution/include" +w$W +h$H +wt1 +pr -a -d -p +fp16 +ocopied.ppm Warning_Level=5 >copied.log 2>&1 ||
    { cat copied.log; exit 1; }
tr '\n' ' ' <copied.log | tr -s '[:space:]' ' ' | grep -F "Prepared interior light grid: 65 x 65 x 65; source 0" >/dev/null ||
    { cat copied.log; echo "domain_cells: a copied light lost its domain_cells"; exit 1; }
# Both light kinds accept the same contextual item at every scene version.
for language in pov pov4; do
    write surface "$language" 'sphere { 0, 1 pigment { rgb 1 } light { brightness 12 domain_cells 64 } }'
    "$POVRAY" +isurface.$language +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >surface.$language.log 2>&1 ||
        { cat surface.$language.log; exit 1; }
done
# Each grid in one interior follows only its own count. In the 2 x 2 x 2 box the medium's 5000 cells are 0.117 wide, 18
# per axis, whatever the light's count or the media default; the refraction grid follows only its own detail (0.3 is a
# 0.15 cell, 14 per axis; 0.6 gives 7) or its own domain_cells at detail auto (500000: 80 per axis; 5000: 18).
grids() {
    name=$1 language=$2 defaults=$3 light=$4 detail=$5
    if [ "$language" = pov ]; then prefix='#'; else prefix=; fi
    write "$name" "$language" "$(printf '%s' "$defaults" | sed "s/@/$prefix/g")
box { -1, 1 hollow pigment { rgbt 1 }
  interior {
    light { brightness 1 samples 1 $light }
    media { method 4 domain_cells 5000 scattering { 1, 0.5 } density { bozo } }
    refraction { function { 0.01 * x } $detail }
  }
}"
    "$POVRAY" +i"$name.$language" +w1 +h1 +wt1 +pr -a -f -d -p -gp Warning_Level=5 >"$name.$language.log" 2>&1 ||
        { cat "$name.$language.log"; exit 1; }
    flat=$(tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ')
    density=$(printf '%s' "$flat" | grep -o "Prepared density grid: [0-9]* x [0-9]* x [0-9]*" | head -1)
    refraction=$(printf '%s' "$flat" | grep -o "Prepared refraction grid: [0-9]* x [0-9]* x [0-9]*" | head -1)
}
expect() {
    [ "$density" = "Prepared density grid: $2" ] && [ "$refraction" = "Prepared refraction grid: $3" ] ||
        { cat "$1.log"; echo "domain_cells: $1 gave '$density' and '$refraction'"; exit 1; }
}
for language in pov pov4; do
    grids plain "$language" '' '' ''
    expect plain.$language '18 x 18 x 18' '80 x 80 x 80'
    grids finelight "$language" '' 'domain_cells 262144' ''
    expect finelight.$language '18 x 18 x 18' '80 x 80 x 80'
    grids mediadefault "$language" '@default { media { domain_cells 262144 } }' '' ''
    expect mediadefault.$language '18 x 18 x 18' '80 x 80 x 80'
    grids detaildefault "$language" '@default { refraction { detail 0.3 } }' '' ''
    expect detaildefault.$language '18 x 18 x 18' '14 x 14 x 14'
    grids statedauto "$language" '@default { refraction { detail 0.3 } }' '' 'detail auto'
    expect statedauto.$language '18 x 18 x 18' '80 x 80 x 80'
    grids stated "$language" '@default { media { domain_cells 5000 } }' '' 'detail 0.6'
    expect stated.$language '18 x 18 x 18' '7 x 7 x 7'
    grids cellsdefault "$language" '@default { refraction { domain_cells 5000 } }' '' ''
    expect cellsdefault.$language '18 x 18 x 18' '18 x 18 x 18'
    grids cellsstated "$language" '@default { refraction { domain_cells 5000 } }' '' 'domain_cells 500000'
    expect cellsstated.$language '18 x 18 x 18' '80 x 80 x 80'
done
# A stated detail past twice the refraction domain_cells is coarsened with a warning that names the count to set.
grids capped pov '' '' 'detail 0.012 domain_cells 1000'
tr '\n' ' ' <capped.pov.log | tr -s '[:space:]' ' ' |
    grep -F "The refraction grid at its detail width 0.006 needs 37259704 cells, above the limit of 2000 (twice its domain_cells)" >/dev/null &&
    tr '\n' ' ' <capped.pov.log | tr -s '[:space:]' ' ' | grep -F "Set the refraction domain_cells to at least 18629852" >/dev/null ||
    { cat capped.pov.log; echo "domain_cells: no coarsening warning for a capped refraction grid"; exit 1; }
for language in pov pov4; do
    if [ "$language" = pov ]; then item='#default { refraction { detail 0 } }'; else item='default { refraction { detail 0 } }'; fi
    write baddetail "$language" "$item"
    if "$POVRAY" +ibaddetail.$language +w1 +h1 +wt1 +pr -a -f -d -p -v -gp >baddetail.$language.log 2>&1; then
        echo "domain_cells: expected a zero refraction default detail in $language to fail" >&2; exit 1
    fi
    tr '\n' ' ' <baddetail.$language.log | tr -s '[:space:]' ' ' |
        grep -F "Refraction detail must be auto or finite and greater than zero, in scene units." >/dev/null ||
        { cat baddetail.$language.log; exit 1; }
done
echo 'domain_cells: ok'
