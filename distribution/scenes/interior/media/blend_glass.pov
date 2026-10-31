// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Glass in a fogged room. Left: glass without media stays clear, because an object without media replaces the media around it. Right: the same glass with an empty media { mix add } lets the fog fill it. +w480 +h270
#version 4.0;
// Method 4 scattering is physical: colours are 4x the classic values with extinction 0.25, keeping the classic haze glow and its extinction.

global_settings { assumed_gamma 1 max_trace_level 12 }
camera { location <0, 1.9, -11> look_at <0, 1.7, 0.3> angle 70 right x * image_width / image_height }
background { rgb 0.01 }
plane { y, 0 pigment { checker rgb 0.25 rgb 0.05 } finish { diffuse 0.8 } }
box { <-8.5, 0, -2.5>, <8.5, 4.5, 4> pigment { rgbt 1 } interior { media { scattering { 1, rgb 1.2 extinction 0.25 } } } }

#declare GlassTexture = texture { pigment { rgbf <0.97, 0.99, 1, 0.96> } finish { specular 0.6 roughness 0.002 reflection { 0.02, 1 fresnel } conserve_energy } }
#declare Clear = material { texture { GlassTexture } interior { ior 1.5 } }
#declare Filled = material { texture { GlassTexture } interior { ior 1.5 media { mix add } } }
#macro Set(Glass, X)
  union {
    light_source { <-5, 6, -3> rgb <1.8, 1.5, 1.1> spotlight point_at <-2.2, 1.2, 0.5> radius 5 falloff 9 }
    light_source { <3.5, 7, -1> rgb <0.6, 0.8, 1.6> spotlight point_at <0.3, 1.25, -0.4> radius 5 falloff 9 }
    sphere { <-2.2, 1.2, 0.5>, 1.2 material { Glass } }
    torus { 0.9, 0.35 rotate <70, 0, 25> translate <0.3, 1.25, -0.4> material { Glass } }
    superellipsoid { <0.25, 0.25> scale <0.8, 1.3, 0.8> rotate y * 35 translate <2.4, 1.3, 1> material { Glass } }
    translate x * X
  }
#end
Set(Clear, -3.7)
Set(Filled, 3.7)
