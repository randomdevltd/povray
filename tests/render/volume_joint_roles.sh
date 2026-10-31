#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_joint_roles.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
. "$SRCDIR/tests/render/image.sh"
roles() {
    image_awk -l joint_media -l volume_joint_roles_reference 'BEGIN { volume_joint_roles_check(ARGV[1], ARGV[2], ARGV[3]) }' "$@"
}
for kind in 0 1 2; do
    render "role$kind" Declare=Case="$kind"
    roles "$kind" "role$kind.ppm" ''
done
render refraction Declare=Case=3
render refraction_control Declare=Case=3 Declare=Control=1
roles 3 refraction.ppm refraction_control.ppm
image_awk '
function last_count(text, pattern,    found) {
    if (!match(text, pattern)) return -1
    found = substr(text, RSTART, RLENGTH)
    match(found, /[0-9]+$/)
    return substr(found, RSTART) + 0
}
BEGIN {
    for (a = 1; a < ARGC; a++) {
        text = ""
        while ((getline line < ARGV[a]) > 0) text = text line "\n"
        close(ARGV[a])
        draws = last_count(text, "Media Fixed Draws:[[:space:]]+[0-9]+[[:space:]]+Classic Draws:[[:space:]]+[0-9]+")
        pairs = last_count(text, "Media Scatter Legs:[[:space:]]+[0-9]+[[:space:]]+Source Pairs:[[:space:]]+[0-9]+")
        if (draws != 0) fail(ARGV[a] ": classic draws " (draws < 0 ? "not reported" : draws) ", expected 0")
        if (!(pairs > 0)) fail(ARGV[a] ": source pairs " (pairs < 0 ? "not reported" : pairs) ", expected some")
    }
}' *.log
