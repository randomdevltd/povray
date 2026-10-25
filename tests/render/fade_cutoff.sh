#!/bin/sh
# fade_cutoff.sh <povray> <srcdir>: a media light's fade_cutoff defaults by version and drops only lights fainter than it.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=64
fail() { echo "fade_cutoff: $*" >&2; exit 1; }
render() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/$scene.pov" +L"$SRCDIR/tests/render" +w$W +h$H -a -d -p +v -gp +fp16 File_Gamma=1.0 +wt2 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
same() { cmp -s "$1.pixels" "$2.pixels" || fail "$1 and $2 differ"; }
differ() { ! cmp -s "$1.pixels" "$2.pixels" || fail "$1 and $2 are the same"; }
drops() { tr -s ' \n' '  ' < "$1.log" | grep -o "[0-9]* light tests dropped by fade_cutoff" | grep -o "^[0-9]*" || echo 0; }
bad() { "$POVRAY" +i"$SRCDIR/tests/render/fade_cutoff.pov" +L"$SRCDIR/tests/render" +w8 +h8 -d -p -gp -f "$@" > bad.log 2>&1 && fail "accepted $*"
        tr -s " \n" "  " < bad.log | grep -q "fade_cutoff must be finite and not negative" || { cat bad.log; fail "no fade_cutoff error for $*"; }; }
# the far light reaches the floor at about 0.002 (unfaded 1/r^2) or 0.004 (fade_distance 1 fade_power 2); the near one at 0.3 or more
for fade in 0 1; do
    render d38_$fade fade_cutoff_38 Declare=Fade=$fade
    render z38_$fade fade_cutoff_38 Declare=Fade=$fade Declare=Cutoff=0
    same d38_$fade z38_$fade
    render n38_$fade fade_cutoff_38 Declare=Fade=$fade Declare=Far=0
    differ d38_$fade n38_$fade
    [ "$(drops d38_$fade)" -eq 0 ] || fail "#version 3.8 dropped light tests by default"
    render d40_$fade fade_cutoff Declare=Fade=$fade
    [ "$(drops d40_$fade)" -eq 0 ] || fail "the 4.0 default dropped a light brighter than it"
    render c40_$fade fade_cutoff Declare=Fade=$fade Declare=Cutoff=0.01
    render n40_$fade fade_cutoff Declare=Fade=$fade Declare=Far=0 Declare=Cutoff=0.01
    render m40_$fade fade_cutoff Declare=Fade=$fade Declare=Far=0 Declare=Cutoff=0
    same c40_$fade n40_$fade
    same n40_$fade m40_$fade
    differ d40_$fade c40_$fade
    [ "$(drops c40_$fade)" -ge $(( W * H )) ] || fail "fade $fade: only $(drops c40_$fade) light tests dropped"
    [ "$(drops n40_$fade)" -eq 0 ] || fail "fade $fade: the near light was dropped"
done
# a very far light falls below the 4.0 default and is kept under 3.8
render v40 fade_cutoff Declare=Far=2
[ "$(drops v40)" -gt 0 ] || fail "the 4.0 default dropped nothing of a very far light"
render v38 fade_cutoff_38 Declare=Far=2
[ "$(drops v38)" -eq 0 ] || fail "#version 3.8 dropped a very far light"
# #default gives media lights parsed after it their starting value; a light's own value overrides it, a #version switch keeps it
render e40 fade_cutoff Declare=DefaultCutoff=0.01
same e40 n40_0
[ "$(drops e40)" -gt 0 ] || fail "#default fade_cutoff dropped nothing"
render s40 fade_cutoff Declare=DefaultCutoff=0.01 Declare=Switch=1
same s40 n40_0
[ "$(drops s40)" -eq "$(drops e40)" ] || fail "a #version switch lost the #default fade_cutoff"
render l40 fade_cutoff Declare=DefaultCutoff=0.01 Declare=DefaultLast=1
same l40 d40_0
[ "$(drops l40)" -eq 0 ] || fail "#default fade_cutoff reached lights parsed before it"
render o40 fade_cutoff Declare=DefaultCutoff=0.01 Declare=Cutoff=0
same o40 d40_0
[ "$(drops o40)" -eq 0 ] || fail "a light's fade_cutoff did not override #default"
bad Declare=Cutoff=-1
bad Declare=DefaultCutoff=-1
echo "fade_cutoff: ok"
