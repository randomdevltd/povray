#version 3.8;
// Case 1-6 places the light (L) and receiver (R) out/in an absorbing medium: 1 L out R out, 2 L in, 3 R in, 4 both in, 5 L in a
// clear cavity, 6 R is the container's face; 7 a scattering medium lit in parallel from Far. Block, Area, NoShadow vary it.
#ifndef (Case) #declare Case = 1; #end
#ifndef (Method) #declare Method = 4; #end
#ifndef (Block) #declare Block = 0; #end
#ifndef (Haze) #declare Haze = 1; #end
#ifndef (Scale) #declare Scale = 1; #end
#ifndef (Area) #declare Area = 0; #end
#ifndef (Far) #declare Far = 2; #end
#ifndef (NoShadow) #declare NoShadow = 0; #end
global_settings { assumed_gamma 1 ambient_light 0 }
background { rgb 0 }
camera { orthographic location <0, 0, -10> look_at 0 right 0.2 * x up 0.15 * y scale Scale }
#declare Light_At = (Case = 7 ? <0, 0, -2> + vnormalize(<1, 1, -1>) * Far : <0, 0, -4>);
#if (Block)
sphere { Light_At, 0.01 hollow no_shadow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness (Case = 6 ? 4700 : 75000) attenuation { cutoff 0 } samples 256 } } scale Scale }
#else
light_source { Light_At, rgb 1 media_attenuation on media_interaction on #if (Case = 7) parallel point_at <0, 0, -2> #end
  #if (Area) area_light 0.4 * x, 0.4 * y, 5, 5 adaptive 1 jitter circular orient #end scale Scale }
#end
#declare White = texture { pigment { rgb 1 } finish { ambient 0 diffuse 1 specular 0 } }
#if (Case != 7) box { <-1, -1, 0>, <1, 1, 0.1> texture { White } scale Scale } #end
#declare Medium = interior { media {
  #if (Method = 4) method 4 #else method 3 intervals 1 samples 4 jitter 0 #end
  absorption rgb 0.1 / Scale } }
#if (Case = 7)
box { <-0.5, -0.5, -2.5>, <0.5, 0.5, -1.5> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { media { #if (Method = 4) method 4 #else method 3 intervals 1 samples 3 jitter 0 #end scattering { 1, 0.5 / Scale } } } scale Scale }
#elseif (Case = 6)
box { <-2, -2, -5>, <2, 2, -1> hollow texture { White } double_illuminate #if (Haze) interior { Medium } #end scale Scale }
#elseif (Haze)
#declare Near = array[5] { -3, -5, -3, -5, -5 };
#declare Back = array[5] { -1, -1, 1, 1, -1 };
#if (Case = 5)
difference { box { <-2, -2, -5>, <2, 2, -1> } box { <-1.5, -1.5, -4.5>, <1.5, 1.5, -3.5> }
#else
box { <-2, -2, Near[Case - 1]>, <2, 2, Back[Case - 1]>
#end
  hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { Medium } #if (NoShadow) no_shadow #end scale Scale }
#end
