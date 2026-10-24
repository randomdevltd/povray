#version 4.0;
// Method 4 haze before a parse tail. Late changes after it 1 the cell limit, 2 the noise generator, 4 the wave count;
// Kind 1 nests a function and 2 tests an object in the density, 3 shares a declared interior, 4 is a merge's; Tagged filters it.
#ifndef (Late) #declare Late = 0; #end
#ifndef (Tail) #declare Tail = 0; #end
#ifndef (Tagged) #declare Tagged = 0; #end
#ifndef (Kind) #declare Kind = 0; #end
#ifndef (Fail) #declare Fail = 0; #end
#include "functions.inc"
global_settings { assumed_gamma 1 #if (Tagged) filter_tags { !"haze" } #end }
background { rgb <0.1, 0.1, 0.15> }
camera { location <0, 2.5, -7> look_at <0, 0.5, 0> }
light_source { <260, 710, 230> rgb 0.9 parallel point_at 0 }
plane { y, -0.2 pigment { rgb 0.4 } }
#declare Haze = interior { media {
  method 4 resolution 0.08
  scattering { 1, 0.4 extinction 0.5 }
  density { function { 0.5 + 0.5 * f_noise3d(x * 3, y * 3, z * 3) } }
  density { crackle turbulence 0.4 density_map { [0 rgb 0.2] [1 rgb 1] } scale 0.4 }
  #switch (Kind)
    #case (1) density { pigment_pattern { function { 0.5 + 0.5 * sin(5 * x) } } density_map { [0 rgb 0.3] [1 rgb 1] } } #break
    #case (2) density { object { sphere { 0, 1 } rgb 0.4, rgb 1 } } #break
  #end
  #switch (Late)
    #case (4) density { waves density_map { [0 rgb 0.5] [1 rgb 1] } scale 0.5 } #break
  #end
} }
#switch (Kind)
  #case (3)
    box { <-3.8, -0.1, -2>, <-0.2, 1.5, 2> hollow pigment { rgbt 1 } interior { Haze } }
    box { <0.2, -0.1, -2>, <3.8, 1.5, 2> hollow pigment { rgbt 1 } interior { Haze } }
  #break
  #case (4)
    merge { box { <-3, -0.1, -2>, <-0.2, 1.5, 2> } box { <0.2, -0.1, -2>, <3, 1.5, 2> } hollow pigment { rgbt 1 } interior { Haze } }
  #break
  #else
    box { <-2, -0.1, -2>, <2, 1.5, 2> hollow pigment { rgbt 1 } interior { Haze } #if (Tagged) tags { "haze" } #end }
#end
#debug "eager media: recorded\n"
#switch (Late)
  #case (1) global_settings { prepared_media_cells 2000000 } #break
  #case (2) global_settings { noise_generator 1 } #break
  #case (4) global_settings { number_of_waves 3 } #break
#end
#declare Acc = 0;
#for (I, 1, Tail) #declare Acc = mod(Acc * 3 + I, 1000); #end
#if (Fail) #error "stop with the grid half built" #end
sphere { <0, 0.5, -2.5>, 0.3 pigment { rgb <1, mod(Acc, 1000) / 1000, 0.2> } }
