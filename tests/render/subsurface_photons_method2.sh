#!/bin/sh
# subsurface_photons_method2.sh <povray> <srcdir>: a method 2 photon caustic reaches a subsurface method 2 slab through its point
# cloud, every shading point is served, the image is the same on one thread and two, and each memory setting keeps its meaning.
set -e
trap 'rm -f ssm2ph_*' EXIT
unset POV_PHOTON_MEMORY
POVRAY=$1; SRCDIR=$2
W=64; H=48
render() {
    name=$1; shift; rc=0
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_photons_method2.pov" +w$W +h$H -d -p -a +fp16 +o"ssm2ph_$name.ppm" "$@" \
        > "ssm2ph_$name.log" 2>&1 || rc=$?
    tr -d ' \r\n' < "ssm2ph_$name.log" > "ssm2ph_$name.txt"
    [ ! -f "ssm2ph_$name.ppm" ] || tail -c $((W * H * 6)) "ssm2ph_$name.ppm" > "ssm2ph_$name.px"
    return $rc
}
fail() { echo "subsurface_photons_method2: $*"; exit 1; }
stat() { sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried,\([0-9]*\)served([0-9]*intheradiositypretrace),\([0-9]*\)leftwithoutsubsurfacelight.*/\1 \2 \3/p' "ssm2ph_$1.txt"; }
served() { set -- $(stat "$1"); [ "$#" -eq 3 ] && [ "$1" -gt 0 ] && [ "$1" -eq "$2" ] && [ "$3" -eq 0 ]; }
gathered() { sed -n 's/.*deposits,k[0-9]*,cap[0-9.]*xbaseradius,\([0-9]*\)contributions.*/\1/p' "ssm2ph_$1.txt"; }
levels() { od -An -v -tu1 "ssm2ph_$1.px" | awk '{ for (k = 1; k <= NF; k++) b[n++] = $k }
    END { for (i = 0; i < n; i += 2) { v = b[i] * 256 + b[i + 1]; if (v > 0) lit++; if (v == 65535) top++; s += v } print lit + 0, top + 0, s + 0 }'; }
check() {
    served "$1" || fail "$2 was not served in full by the cloud: $(stat "$1")"
    ! grep -q 'Subsurfacemethod2\(fellback\|left\)' "ssm2ph_$1.txt" || fail "$2 handed shading points over or left them unlit"
    [ "$(gathered "$1")" -gt 0 ] || fail "$2 gathered no photon method 2 deposits"
    set -- $(levels "$1") "$2"
    [ "$1" -gt 0 ] || fail "$4 received no photon light"
    [ "$2" -eq 0 ] || fail "$4 has $2 channels at full scale, as a NaN would leave them"
}

render dark +wt2 Declare=Photons=0
set -- $(levels dark)
[ "$1" -eq 0 ] || fail "a photon-only lamp lit the slab without photons"
render one +wt1
render two +wt2
check one "the slab on one thread"
check two "the slab on two threads"
cmp -s ssm2ph_one.px ssm2ph_two.px || fail "one thread and two rendered different photon light"
render lit_one +wt1 Declare=PhotonOnly=0
render lit_two +wt2 Declare=PhotonOnly=0
served lit_one || fail "the directly lit slab was not served in full: $(stat lit_one)"
cmp -s ssm2ph_lit_one.px ssm2ph_lit_two.px || fail "one thread and two rendered the directly lit slab differently"
render collect +wt2 Declare=Case=1
check collect "the slab with a half that does not collect"
render group +wt2 Declare=Case=2
check group "the slab in a light group"

if render capped +wt2 Declare=Spacing=0.0001 Declare=SubsurfaceMemory=1; then fail "a cloud past its 1 MB cap did not stop the render"; fi
grep -q 'Subsurfacecloudsneedatleast[0-9.]*MBatspacing0.0001after.*pasttheirmemorycapof1MB' ssm2ph_capped.txt ||
    fail "the subsurface memory cap was not reported in MB beside a photons memory"
if render photon_cap +wt2 Declare=PhotonMemory=0.000001; then fail "a photon map past its ceiling did not stop the render"; fi
grep -q "GBallowed(setbythescene'sphotonsmemory)" ssm2ph_photon_cap.txt ||
    fail "the photons memory ceiling was not reported in GB beside a subsurface memory"
echo "subsurface_photons_method2: method 2 photons reach the subsurface cloud, on any thread count, and both memory settings hold"
