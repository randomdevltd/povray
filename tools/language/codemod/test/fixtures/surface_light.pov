#version 3.7;
global_settings { assumed_gamma 1 }
box { <-1,1,-1>, <1,1.1,1> pigment { rgb <1,0.5,0.2> } finish { emission 2 } light { brightness 2 samples 8 media_attenuation on bounce { passes 3 } } }
sphere { <0,2,0>, 0.2 light { brightness 3 samples 4 bounce } }
sphere { <0,3,0>, 0.2 light { brightness scale_lumens(800, 0.5) lumens } }
sphere { <0,0,0>, 1 pigment { rgbt 1 } hollow interior { media { scattering { 1, rgb 0.3 } emission rgb 2 } light { bounce { passes 2 self rgb <0.1,0.03,0> } } } }

#default { interior { light { domain_cells 500000 } } light { domain_cells 4096 } }
sphere { 0, 1 light { bounce { passes 1 } domain_cells 64 } }
