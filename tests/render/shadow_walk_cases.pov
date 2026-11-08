#version 3.8;
// Shadow-walk regression cases, one per Case (Declare=Case=N): each puts many shadowing boundaries between lights and a floor.
#ifndef (Case) #declare Case = 1; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 max_trace_level 8
  #if (Case = 9) photons { count 20000 autostop 0 jitter 0 } #end
  #if (Case = 10) subsurface { method 2 samples 40, 8 } #end
}
camera { location <0, 9, -11> look_at <0, 0.8, 0> angle 42 }
#declare Floor = plane { y, 0 pigment { checker rgb 0.85, rgb 0.6 scale 0.5 } finish { ambient 0 diffuse 0.9 } }
#declare Clear = texture { pigment { rgbf <0.8, 0.95, 1, 0.85> } finish { ambient 0 diffuse 0.05 specular 0.3 } }
#declare Tint = texture { pigment { rgbf <1, 0.7, 0.5, 0.8> } finish { ambient 0 diffuse 0.05 } }
#declare Matte = texture { pigment { rgb <0.7, 0.5, 0.3> } finish { ambient 0 diffuse 0.8 } }
#declare Point = light_source { <3, 12, -4> rgb 1 }
#declare Area = light_source { <-4, 10, -2> rgb 0.8 area_light x * 2, z * 2, 4, 4 adaptive 1 circular orient }
#macro Grid(N, Size, Gap, Y, Tex)
  #for (I, 0, N - 1) #for (J, 0, N - 1)
    box { -Size, Size translate <(I - (N - 1) / 2) * Gap, Y, (J - (N - 1) / 2) * Gap> texture { Tex } }
  #end #end
#end

object { Floor }
#switch (Case)
#case (1) // mostly opaque: a 16x16 grid of matte blocks above a few clear ones, a point and a classic area light
  object { Point } object { Area }
  Grid(16, 0.12, 0.36, 2.5, Matte)
  Grid(4, 0.3, 1.4, 1.2, Clear)
#break
#case (2) // patterned transparency: filter and transmit set by pattern, layered textures and a texture_map
  object { Point } object { Area }
  #for (K, 0, 5)
    box { <-3, 0.3, -0.04>, <3, 3, 0.04> rotate y * K * 15 translate z * (K - 2.5) * 0.4
      texture { pigment { checker rgbf <1, 1, 1, 0.9>, rgb <0.2, 0.3, 0.8> scale 0.3 } finish { ambient 0 diffuse 0.3 } }
      texture { pigment { bozo color_map { [0 rgbt 1] [0.6 rgbt 1] [0.6 rgbf <1, 0.5, 0.2, 0.6>] [1 rgbf <1, 0.5, 0.2, 0.6>] } scale 0.4 } }
    }
  #end
  sphere { <1.5, 1, -2>, 0.9 texture { leopard texture_map { [0 Clear] [0.5 Clear] [0.5 Matte] [1 Matte] } scale 0.15 } }
#break
#case (3) // interior media and fog with shadows through media-filled clear boxes
  light_source { <3, 12, -4> rgb 1 media_interaction on media_attenuation on }
  fog { distance 30 rgb 0.6 fog_type 2 fog_alt 1.5 fog_offset 0 }
  #for (K, 0, 4)
    box { <-0.5, 0.5, -0.5>, <0.5, 1.5, 0.5> translate <(K - 2) * 1.2, 0, 0> hollow
      texture { Clear } interior { media { absorption <0.1, 0.4, 0.6> * K scattering { 1, 0.2 } samples 3 method 3 intervals 1 } } }
  #end
  sphere { <0, 2.6, 0>, 0.8 hollow texture { Tint } interior { media { absorption 0.5 } } }
#break
#case (4) // isosurfaces and blobs: many roots per shadow ray
  object { Point } object { Area }
  isosurface { function { y - 1.2 - 0.25 * sin(6 * x) * cos(6 * z) } contained_by { box { <-3, 0.5, -3>, <3, 2, 3> } } max_gradient 3
    texture { Clear } }
  isosurface { function { sqrt(x * x + z * z) - 0.5 - 0.15 * sin(12 * y) } contained_by { box { <-1, 0, -1>, <1, 4, 1> } } max_gradient 3
    translate <2, 0, -1.5> texture { Tint } }
  blob { threshold 0.5 #for (K, 0, 24) sphere { <mod(K, 5) - 2, 2.8 + 0.2 * mod(K, 3), int(K / 5) - 2>, 0.8, 1 } #end texture { Tint } }
#break
#case (5) // CSG: difference, intersection, merge and inverse with clear and opaque parts
  object { Point } object { Area }
  difference { box { <-1, 0.3, -1>, <1, 2.3, 1> } #for (K, 0, 6) sphere { <K * 0.3 - 0.9, 2.3, 0>, 0.35 } #end translate x * -2 texture { Clear } }
  intersection { sphere { 0, 1.2 } box { -0.9, 0.9 } cylinder { -x * 2, x * 2, 0.7 inverse } translate <0.5, 1.4, 0> texture { Tint } }
  merge { #for (K, 0, 5) sphere { <K * 0.35, 0, 0>, 0.5 } #end translate <1.5, 2.6, -1.5> texture { Clear } }
  union { #for (K, 0, 5) sphere { <K * 0.35, 0, 0>, 0.5 } #end translate <1.5, 2.6, 1.0> texture { Clear } }
  difference { sphere { 0, 0.8 } sphere { 0, 0.6 } translate <-2, 3.4, -1.5> texture { Matte } }
#break
#case (6) // no_shadow, shadowless and fill lights among clear and opaque blockers
  object { Point }
  light_source { <-4, 10, -2> rgb 0.5 shadowless }
  light_source { <0, 8, -6> rgb 0.3 area_light x, z, 3, 3 }
  Grid(5, 0.3, 1.0, 1.4, Clear)
  #for (K, 0, 4) sphere { <K - 2, 2.6, -1.0>, 0.35 texture { Matte } no_shadow } #end
  #for (K, 0, 4) sphere { <K - 2, 2.6, 1.0>, 0.35 texture { Tint } no_shadow } #end
  box { <-3, 3.2, -0.2>, <3, 3.3, 0.2> texture { Matte } }
#break
#case (7) // coincident surfaces: clear boxes face to face, separately and in a union, a mesh face on a box face, a doubled face
  object { Point } object { Area }
  #for (K, 0, 5) box { <-2.5, 0.5 + K * 0.3, -0.6>, <-1.0, 0.8 + K * 0.3, 0.6> texture { Clear } } #end
  union { #for (K, 0, 5) box { <-0.7, 0.5 + K * 0.3, -0.6>, <0.7, 0.8 + K * 0.3, 0.6> } #end texture { Tint } }
  box { <1.0, 0.5, -0.6>, <2.5, 1.5, 0.6> texture { Clear } }
  mesh { triangle { <1.0, 1.5, -0.6>, <2.5, 1.5, -0.6>, <2.5, 1.5, 0.6> } triangle { <1.0, 1.5, -0.6>, <2.5, 1.5, 0.6>, <1.0, 1.5, 0.6> }
         texture { Tint } }
  box { <1.0, 1.5, -0.6>, <2.5, 2.5, 0.6> texture { Tint } }
  box { <-1, 2.8, 1>, <1, 3.0, 2> texture { Clear } } box { <-1, 2.8, 1>, <1, 3.0, 2> texture { Clear } }
#break
#case (8) // many clear walls between surface (object) lights and the floor
  light_source { <3, 12, -4> rgb 0.4 }
  box { <-1.5, 6, -1.5>, <1.5, 6.1, 1.5> pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
    light { pigment { rgb <1, 0.9, 0.8> } brightness 6 nits samples 8 } }
  sphere { <3, 4, 2>, 0.3 pigment { rgbt 1 } hollow interior { light { brightness 300 lumens samples 8 } } }
  #for (L, 0, 5) Grid(6, 0.22, 0.5, 1.0 + L * 0.5, Clear) #end
#break
#case (9) // photons through clear walls and a glass sphere
  light_source { <3, 12, -4> rgb 1 photons { refraction on reflection on } }
  sphere { <0, 1.6, 0>, 1 texture { pigment { rgbf <1, 1, 1, 0.95> } finish { ambient 0 diffuse 0 reflection 0.05 } }
    interior { ior 1.5 } photons { target refraction on reflection on } }
  Grid(4, 0.3, 1.2, 3.2, Clear)
#break
#case (10) // subsurface (method 2) receivers lit through clear blockers
  object { Point } object { Area }
  Grid(4, 0.3, 1.2, 3.2, Clear)
  #for (K, 0, 2)
    sphere { <(K - 1) * 1.8, 0.8, 0>, 0.8 texture { pigment { rgb <0.9, 0.6, 0.5> } finish { ambient 0 diffuse 0.6 subsurface { translucency <1, 0.5, 0.3> } } }
      interior { ior 1.4 } }
  #end
#break
#case (13) // clear shapes whose root search stops at the nearest root outside a compound: mesh without inside vector,
           // superellipsoid, lathe, sor, height field and blob
  object { Point } object { Area }
  mesh { #for (L, 0, 3) #local Y = 2.4 + L * 0.25;
    triangle { <-3, Y, 0.6>, <-1.4, Y, 0.6>, <-1.4, Y, 2.2> } triangle { <-3, Y, 0.6>, <-1.4, Y, 2.2>, <-3, Y, 2.2> } #end
    texture { Tint } }
  superellipsoid { <0.3, 0.3> scale <0.6, 0.3, 0.6> translate <-0.6, 2.4, 1.4> texture { Clear } }
  lathe { cubic_spline 6, <0.1, 0>, <0.7, 0.1>, <0.3, 0.5>, <0.7, 0.9>, <0.3, 1.2>, <0.1, 1.3> translate <0.9, 1.6, 1.4> texture { Clear } }
  sor { 6, <0.1, 0>, <0.7, 0.1>, <0.3, 0.5>, <0.7, 0.9>, <0.3, 1.2>, <0.1, 1.3> translate <2.4, 1.6, 1.4> texture { Tint } }
  height_field { function 40, 40 { 0.5 + 0.5 * sin(x * 12) * cos(y * 12) } scale <2.4, 0.4, 2.4> translate <-1.2, 2.6, -2.2> texture { Clear } }
  blob { threshold 0.5 #for (K, 0, 5) sphere { <K * 0.45, 0, 0>, 0.5, 1 } #end translate <-2.6, 2.4, -1.2> texture { Tint } }
#break
#case (12) // a clear parametric torus over clear blocks: many roots per shadow ray from a slow shape
  object { Point }
  parametric { function { cos(u) * (1 + 0.3 * cos(v)) } function { 0.3 * sin(v) } function { sin(u) * (1 + 0.3 * cos(v)) }
    <0, 0>, <2 * pi, 2 * pi> contained_by { box { <-1.4, -0.4, -1.4>, <1.4, 0.4, 1.4> } } precompute 12 x, y, z accuracy 0.05 max_gradient 2
    translate <-2, 1, -1.5> texture { Clear } }
  Grid(3, 0.3, 1.2, 0.6, Tint)
#break
#case (11) // classic area lights, a spotlight and a cylinder light through a grid of clear and tinted blocks
  light_source { <-4, 10, -2> rgb 0.6 area_light x * 2, z * 2, 5, 5 adaptive 1 circular orient }
  light_source { <4, 9, -3> rgb 0.6 spotlight point_at 0 radius 25 falloff 35 area_light x, z, 3, 3 }
  light_source { <0, 8, 4> rgb 0.4 cylinder point_at <0, 0, 0> radius 3 falloff 4 }
  Grid(6, 0.25, 0.8, 1.5, Clear)
  Grid(3, 0.3, 1.6, 2.6, Tint)
#break
#case (15) // the cached blocker churns between small opaque blockers and an unsplit union bounded_by a box that holds a plane
  object { Point } object { Area }
  union { plane { y, 1.0 } sphere { <0.6, 1.5, 0>, 0.4 } box { <-1.2, 1.2, -0.4>, <-0.4, 1.6, 0.4> }
    bounded_by { box { <-1.5, 0.8, -1.5>, <1.5, 2.0, 1.5> } } split_union off texture { Matte } }
  Grid(8, 0.1, 0.6, 2.8, Matte)
#break
#case (14) // fog and an attenuating light over opaque blocks, some under media-filled clear slabs: many shadow rays end black
  light_source { <3, 12, -4> rgb 1 media_interaction on media_attenuation on }
  fog { distance 30 rgb 0.6 fog_type 2 fog_alt 1.5 fog_offset 0 }
  Grid(8, 0.14, 0.6, 1.2, Matte)
  #for (K, 0, 2)
    box { <-2.4, 2.0 + K * 0.4, -2.4>, <0.4, 2.1 + K * 0.4, 2.4> hollow
      texture { Clear } interior { media { absorption <0.1, 0.3, 0.5> scattering { 1, 0.1 } samples 3 method 3 intervals 1 } } }
  #end
#break
#end
