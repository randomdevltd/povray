#!/bin/sh
# deleted_media_syntax.sh <povray> <srcdir>: media resolution, prepared_media_cells, prepared_source_cells and the
# Prepared_Media_Cells option are gone, so each is an unknown word; domain_cells, scene-wide and per medium, renders.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
write() {
    { if [ "$2" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
      echo 'global_settings { assumed_gamma 1 }'
      printf '%s\n' "$3"
    } >"$1.$2"
}
# unknown <name> <word> <scene text>: the scene on line 3 fails to parse at the word, an undeclared identifier in the
# classic language; the 4.0 checker may name the word instead
unknown() {
    name=$1; word=$2; text=$3
    for language in pov pov4; do
        write "$name" "$language" "$text"
        if "$POVRAY" +i"$name.$language" +w1 +h1 +wt1 -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            cat "$name.$language.log"; echo "deleted_media_syntax: $name in $language parsed" >&2; exit 1
        fi
        flat=$(tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ')
        if [ "$language" = pov ]; then
            printf '%s\n' "$flat" | grep -F "line 3: Parse Error: No matching }, undeclared identifier found instead" >/dev/null
        else
            printf '%s\n' "$flat" | grep -E "'$word'|undeclared identifier" >/dev/null
        fi || { cat "$name.$language.log"; echo "deleted_media_syntax: $name in $language is not an unknown word" >&2; exit 1; }
    done
}
unknown resolution resolution 'box { -1, 1 hollow pigment { rgbt 1 } interior { media { method 4 resolution 0.1 absorption 0.5 } } }'
unknown media_cells prepared_media_cells 'global_settings { prepared_media_cells 4096 }'
unknown source_cells prepared_source_cells 'global_settings { prepared_source_cells 4096 }'
# the option is unknown on the command line and in an INI file
write plain pov 'box { -1, 1 hollow pigment { rgbt 1 } interior { media { method 4 absorption 0.5 } } }'
if "$POVRAY" +iplain.pov +w1 +h1 +wt1 -f -d -p -v -gp Prepared_Media_Cells=4096 >option.log 2>&1; then
    cat option.log; echo "deleted_media_syntax: Prepared_Media_Cells was accepted on the command line" >&2; exit 1
fi
grep -F "Cannot process command-line at 'Prepared_Media_Cells=4096'" option.log >/dev/null || { cat option.log; exit 1; }
printf 'Prepared_Media_Cells=4096\n' >cells.ini
if "$POVRAY" cells.ini +iplain.pov +w1 +h1 +wt1 -f -d -p -v -gp >ini.log 2>&1; then
    cat ini.log; echo "deleted_media_syntax: Prepared_Media_Cells was accepted in an INI file" >&2; exit 1
fi
grep -F "Unknown key 'Prepared_Media_Cells' in INI file." ini.log >/dev/null || { cat ini.log; exit 1; }
# the replacement parses and renders
write cells pov '#default { media { domain_cells 4096 } }
camera { location <0, 0, -4> look_at 0 }
light_source { <2, 4, -3> rgb 1 }
box { -1, 1 hollow pigment { rgbt 1 }
  interior { media { method 4 domain_cells 4096 scattering { 1, 0.3 } density { bozo scale 0.4 } } } }
sphere { 0, 0.5 hollow pigment { rgbt 1 } interior { media { method 4 absorption 0.5 density { spherical } } } }'
"$POVRAY" +icells.pov +w8 +h8 +wt1 -a -d -p -v -gp +fp16 +ocells.ppm >cells.log 2>&1 || { cat cells.log; exit 1; }
[ -s cells.ppm ] || { cat cells.log; echo "deleted_media_syntax: domain_cells wrote no image" >&2; exit 1; }
echo 'deleted_media_syntax: ok'
