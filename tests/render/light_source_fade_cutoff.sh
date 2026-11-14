#!/bin/sh
# light_source_fade_cutoff.sh <povray> <srcdir>: a faded light_source's fade_cutoff defaults by version, drops only lights
# fainter than it and never an unfaded light; #default sets it for light_sources and for light blocks.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=64
fail() { echo "light_source_fade_cutoff: $*" >&2; exit 1; }
render() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/$scene.pov" +L"$SRCDIR/tests/render" +w$W +h$H -a -d -p +v -gp +fp16 File_Gamma=1.0 +wt2 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
same() { cmp -s "$1.pixels" "$2.pixels" || fail "$1 and $2 differ"; }
differ() { ! cmp -s "$1.pixels" "$2.pixels" || fail "$1 and $2 are the same"; }
drops() { tr -s ' \n' '  ' < "$1.log" | grep -o "[0-9]* light_source tests dropped by fade_cutoff" | grep -o "^[0-9]*" || echo 0; }
bad() { msg=$1; shift
        "$POVRAY" +i"$SRCDIR/tests/render/light_source_fade_cutoff.pov" +L"$SRCDIR/tests/render" +w8 +h8 -d -p -gp -f "$@" > bad.log 2>&1 && fail "accepted $*"
        tr -s " \n" "  " < bad.log | grep -q "$msg" || { cat bad.log; fail "no error '$msg' for $*"; }; }

# 0.01 reaches about 14 from a far lamp (1 rgb, fade_distance 1, power 2) and 9 from a near one; the floor is 40 from the far lamps
for variant in "FadeDistance=1" "FadeDistance=0" "Area=1"; do
    v=$(echo "$variant" | tr -d '=')
    render n_$v light_source_fade_cutoff Declare=$variant Declare=Far=0
    render d_$v light_source_fade_cutoff Declare=$variant
    render z_$v light_source_fade_cutoff Declare=$variant Declare=Cutoff=0
    render c_$v light_source_fade_cutoff Declare=$variant Declare=Cutoff=0.01
    render e_$v light_source_fade_cutoff Declare=$variant Declare=DefaultCutoff=0.01
    render o_$v light_source_fade_cutoff_38 Declare=$variant Declare=Cutoff=0.01
    render m_$v light_source_fade_cutoff_38 Declare=$variant Declare=Far=0
    same d_$v z_$v
    differ d_$v n_$v
    same c_$v n_$v
    same e_$v n_$v
    same o_$v m_$v
    [ "$(drops d_$v)" -eq 0 ] || fail "$v: the 4.0 default dropped a light brighter than it"
    [ "$(drops n_$v)" -eq 0 ] || fail "$v: a near light was dropped"
    [ "$(drops c_$v)" -ge $(( W * H )) ] || fail "$v: only $(drops c_$v) light tests dropped"
    [ "$(drops e_$v)" -eq "$(drops c_$v)" ] || fail "$v: #default dropped $(drops e_$v), the light's own cutoff $(drops c_$v)"
done
# the 3.8 default is 0; an unfaded light never takes the test
render o38 light_source_fade_cutoff_38 Declare=Cutoff=0
render d38 light_source_fade_cutoff_38
same o38 d38
[ "$(drops d38)" -eq 0 ] || fail "#version 3.8 dropped light tests by default"
render u light_source_fade_cutoff Declare=Fade=0 Declare=Cutoff=1
render un light_source_fade_cutoff Declare=Fade=0 Declare=Far=0 Declare=Cutoff=1
differ u un
[ "$(drops u)" -eq 0 ] || fail "an unfaded light was dropped"
# a light block's cutoff from #default { light { } } covers surface lights; the interior route leaves them alone
render s light_source_fade_cutoff Declare=Surface=1
render sl light_source_fade_cutoff Declare=Surface=1 Declare=DefaultLightCutoff=10
render so light_source_fade_cutoff Declare=Surface=1 Declare=SurfaceCutoff=10
render si light_source_fade_cutoff Declare=Surface=1 Declare=DefaultInteriorCutoff=10
render s0 light_source_fade_cutoff Declare=Surface=0
differ s s0
same sl so
same sl s0
same si s

bad "fade_cutoff must be finite and not negative" Declare=Cutoff=-1
bad "fade_cutoff must be finite and not negative" Declare=DefaultCutoff=-1
bad "cutoff must be finite and not negative" Declare=DefaultLightCutoff=-1
echo "light_source_fade_cutoff: ok"
