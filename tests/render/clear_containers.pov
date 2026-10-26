// Clear surfaces whose lights add nothing (rgbt 1, rgbf 1, diffuse 0) beside ones whose lights add something; Force=1
// gives the former an invisible highlight so their light loops run, and clear_containers.sh requires the same pixels.
#version 3.7;
#ifndef (Force) #declare Force = 0; #end
#declare Hint = finish { #if (Force) phong 1e-30 #end }
global_settings { assumed_gamma 1 max_trace_level 10 }
camera { location <0, 6, -9> look_at <0, 0.5, 0> angle 55 }
light_source { <-6, 9, -5> rgb 0.9 area_light 1.5 * x, 1.5 * z, 3, 3 jitter }
light_source { <6, 7, -8> rgb 0.5 }
light_source { <0, 1, 0.5> rgb <1, 0.6, 0.3> fade_distance 1 fade_power 2 }
plane { y, -1 pigment { checker rgb 0.9, rgb 0.3 } }
// emitting and absorbing media in an rgbt 1 container, and scattering media in an rgbf 1 one
box { <-4, -0.9, -1>, <-2, 1.5, 1> hollow pigment { rgbt 1 } finish { Hint } interior { media { emission 0.4 absorption 0.5 } } }
sphere { <0, 0.6, 0>, 1.3 hollow pigment { rgbf 1 } finish { Hint } interior { media { scattering { 1, 0.4 } } } }
// a clear layer over a coloured one, and an opaque-looking box with diffuse 0
box { <2, -0.9, -1>, <3.6, 0.8, 0.6> texture { pigment { rgb <0.8, 0.4, 0.2> } } texture { pigment { rgbt 1 } finish { Hint } } }
box { <2.4, 1, -0.6>, <3.4, 2, 0.4> pigment { rgb 0.7 } finish { Hint diffuse 0 ambient 0.1 } }
// clear surfaces whose lights do add something: a highlight, crand, iridescence on a diffuse surface
sphere { <-2, 0.3, -2.5>, 0.6 pigment { rgbt 1 } finish { phong 0.8 } }
sphere { <0, 0.3, -2.8>, 0.6 pigment { rgbt <1, 1, 1, 0.9> } finish { crand 0.3 } }
sphere { <2, 0.3, -2.5>, 0.6 pigment { rgbt 1 } finish { Hint irid { 0.4 thickness 0.5 } } }
