#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
for source in 0 1 2 3 4; do
    disabled=1
    if [ "$source" -eq 4 ]; then disabled=0; fi
    for method in 1 2; do
        "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w8 +h8 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 +o"progressive_direct_${source}_${method}.ppm" \
            Declare=Source="$source" Declare=Method="$method" Declare=Disabled="$disabled" +wt1 \
            > "progressive_direct_${source}_${method}.log" 2>&1 || { cat "progressive_direct_${source}_${method}.log"; exit 1; }
    done
done
for source in 0 1 2 3 4; do
    tail -c 384 "progressive_direct_${source}_1.ppm" > progressive_direct_classic.px
    tail -c 384 "progressive_direct_${source}_2.ppm" > progressive_direct_progressive.px
    od -An -v -t u1 progressive_direct_classic.px progressive_direct_progressive.px | awk '
        {for(i=1;i<=NF;i++) {
            if(++bytes%2) hi=$i;
            else {
                value=256*hi+$i;
                if(++channel<=192) {original[channel]=value; total+=value;}
                else {difference=value-original[channel-192]; if(difference>2 || difference< -2) failed=1;}
            }
        }}
        END {exit failed || total<=100}'
done
for source in 2 3; do
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_direct_lights.pov" +w1 +h1 +pr -a -d -p -v -f \
        Declare=Source="$source" Declare=Disabled=0 Declare=Power=0 > progressive_direct_zero.log 2>&1 || { cat progressive_direct_zero.log; exit 1; }
done
rm -f progressive_direct_*.px progressive_direct_*.ppm progressive_direct_*.log progressive_direct_*.pov-state
echo 'progressive_direct_lights: direct-light power, disabled and zero emitters passed'
