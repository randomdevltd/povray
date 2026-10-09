// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Glass without media in a fogged room stays clear: an object without media replaces the media around it by default. +w480 +h270; Declare=Fill=1 gives the glass an empty media { mix add }, so the fog fills it.
#version 4.0;
#ifndef (Fill) #declare Fill = 0; #end

global_settings { assumed_gamma 1 max_trace_level 12 }
camera { location <0, 2.2, -8> look_at <0, 1.2, 0> angle 50 }
background { rgb 0.01 }
light_source { <-7, 6, -3> rgb <1.8, 1.5, 1.1> spotlight point_at <-2.2, 1.2, 0.5> radius 4 falloff 7 }
light_source { <7, 7, -1> rgb <0.6, 0.8, 1.6> spotlight point_at <0.3, 1.25, -0.4> radius 4 falloff 7 }
plane { y, 0 pigment { checker rgb 0.25 rgb 0.05 } finish { diffuse 0.8 } }
box { <-6, 0, -2.5>, <6, 4.5, 4> pigment { rgbt 1 } interior { media { scattering { 1, rgb 0.3 } } } }

#declare Glass = material {
  texture { pigment { rgbf <0.97, 0.99, 1, 0.96> } finish { specular 0.6 roughness 0.002 reflection { 0.02, 1 fresnel } conserve_energy } }
  interior { ior 1.5 #if (Fill) media { mix add } #end }
}
sphere { <-2.2, 1.2, 0.5>, 1.2 material { Glass } }
torus { 0.9, 0.35 rotate <70, 0, 25> translate <0.3, 1.25, -0.4> material { Glass } }
superellipsoid { <0.25, 0.25> scale <0.8, 1.3, 0.8> rotate y * 35 translate <2.4, 1.3, 1> material { Glass } }
