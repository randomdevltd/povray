#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_photons.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +wt2 +ss0 +fp16 File_Gamma=1 +o"media_prepared_photons_$name.ppm" "$@" \
        > "media_prepared_photons_$name.log" 2>&1 || { cat "media_prepared_photons_$name.log"; exit 1; }
}
for method in 1 2; do
    render "${method}_clear" Declare=PhotonMethod="$method" Declare=Absorbing=0
    render "${method}_constant" Declare=PhotonMethod="$method"
    render "${method}_varying" Declare=PhotonMethod="$method" Declare=Varying=1
done
render 2_inside_clear Declare=PhotonMethod=2 Declare=InsideSource=1 Declare=Absorbing=0
render 2_inside_constant Declare=PhotonMethod=2 Declare=InsideSource=1
render 2_inside_varying Declare=PhotonMethod=2 Declare=InsideSource=1 Declare=Varying=1
python3 "$SRCDIR/tests/render/media_prepared_photons_reference.py"
if "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_photons.pov" +w12 +h12 +pr -a -d -p -v -gp \
    +wt2 -f Declare=PhotonMethod=1 Declare=InsideSource=1 Declare=NativePointInside=1 \
    > media_prepared_photons_native_inside.log 2>&1; then
    cat media_prepared_photons_native_inside.log; exit 1
fi
grep -Fq 'Native photon method 1 requires source-origin support outside prepared scattering media; use photon method 2.' media_prepared_photons_native_inside.log
rm -f media_prepared_photons_*.ppm media_prepared_photons_*.log
echo 'media_prepared_photons: finite constant and varying photon-ray Beer attenuation passed'
