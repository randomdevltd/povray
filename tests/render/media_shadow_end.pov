#version 3.8;
// Declare=Case=1 light and receiver outside the medium, 2 light inside, 3 receiver inside, 4 both inside, 5 light in a clear
// cavity of the medium, 6 the light inside a medium whose container's face is the receiver; Method 3 or 4; Block=1 a light block instead of a point light; Haze=0 drops the medium; Scale sizes the scene.
#ifndef (Case) #declare Case = 1; #end
#ifndef (Method) #declare Method = 4; #end
#ifndef (Block) #declare Block = 0; #end
#ifndef (Haze) #declare Haze = 1; #end
#ifndef (Scale) #declare Scale = 1; #end
global_settings { assumed_gamma 1 ambient_light 0 }
background { rgb 0 }
camera { orthographic location <0, 0, -10> look_at 0 right 0.2 * x up 0.15 * y scale Scale }
#declare Light_At = <0, 0, -4>;
#if (Block)
sphere { Light_At, 0.01 hollow no_shadow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness (Case = 6 ? 4700 : 75000) attenuation { cutoff 0 } samples 256 } } scale Scale }
#else
light_source { Light_At, rgb 1 media_attenuation on media_interaction on scale Scale }
#end
#declare White = texture { pigment { rgb 1 } finish { ambient 0 diffuse 1 specular 0 } }
box { <-1, -1, 0>, <1, 1, 0.1> texture { White } scale Scale }
#declare Medium = interior { media {
  #if (Method = 4) method 4 #else method 3 intervals 1 samples 4 jitter 0 #end
  absorption rgb 0.1 / Scale } }
#if (Case = 6)
box { <-2, -2, -5>, <2, 2, -1> hollow texture { White } double_illuminate #if (Haze) interior { Medium } #end scale Scale }
#elseif (Haze)
#declare Near = array[5] { -3, -5, -3, -5, -5 };
#declare Far = array[5] { -1, -1, 1, 1, -1 };
#if (Case = 5)
difference { box { <-2, -2, -5>, <2, 2, -1> } box { <-1.5, -1.5, -4.5>, <1.5, 1.5, -3.5> }
#else
box { <-2, -2, Near[Case - 1]>, <2, 2, Far[Case - 1]>
#end
  hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { Medium } scale Scale }
#end
