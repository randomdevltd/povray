#!/bin/sh
# photon_hidden_target.sh <povray> <srcdir>: hidden targets (cases 1-3) and a no_shadow panel (4) store what the unmarked target does; panels 6 and 7 block.
set -e
trap 'rm -f photon_hidden_*.log' EXIT
stored() {
    log="photon_hidden_$1.log"
    "$POVRAY" +i"$SRCDIR/tests/render/photon_hidden_target.pov" +w16 +h12 -d -p -a -f +wt1 \
        Declare=Case=$1 Declare=Spacing=0.1 > "$log" 2>&1 ||
        { echo "photon_hidden_target: case $1 failed to render" >&2; return 1; }
    grep -q '^Number of photons shot:' "$log" ||
        { echo "photon_hidden_target: case $1 printed no photon statistics" >&2; return 1; }
    n=$(sed -n 's/^Surface photons stored: *\([0-9][0-9]*\).*/\1/p' "$log")
    echo "${n:-0}"
}
expect() {
    n=$(stored $1)
    [ "$n" = "$2" ] || { echo "photon_hidden_target: case $1 stored $n photons, expected $2"; exit 1; }
}
POVRAY=$1; SRCDIR=$2
plain=$(stored 0)
[ "$plain" -gt 0 ] || { echo "photon_hidden_target: the unmarked target stored no photons"; exit 1; }
for c in 1 2 3 4; do expect $c "$plain"; done
expect 6 0
expect 7 0
echo "photon_hidden_target: hidden targets store $plain photons, as an unmarked one does; panels 6 and 7 block them"
