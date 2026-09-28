// Dark skin over bright flesh: a body with a thin ear and a finger, lit from behind and from the side.
// Case 0: a dark pigment; 1: a half-transparent blue layer over a flesh layer; 2: a dark pigment with a flesh colour;
// 3: flesh with veins below the surface; 4: the veins glowing; 5: the skin's thickness as a pattern; 6: a flesh pigment,
// no skin. Sub=0: no subsurface; Flat=1: plain veins and glow, to time the lookups without their pattern.
#version 3.7;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Sub) #declare Sub = 1; #end
#ifndef (Diffuse) #declare Diffuse = 60; #end
#ifndef (Method) #declare Method = 1; #end
#ifndef (Flat) #declare Flat = 0; #end
#ifndef (Spacing) #declare Spacing = 1; #end
global_settings { assumed_gamma 1.0 mm_per_unit 40 #if (Sub) subsurface { samples Diffuse, 12 method Method spacing Spacing } #end }
#declare Flesh = <0.9, 0.45, 0.35>;
#ifndef (Dark) #declare Dark = <0.16, 0.09, 0.06>; #end
#declare Mfp = <3, 1.2, 0.6>;
#if (Flat)
  #declare Veins = pigment { rgb Flesh }
  #declare Glow = pigment { rgb 0.1 }
#else
  #declare Veins = pigment { marble turbulence 0.6 colour_map { [0 rgb Flesh] [0.7 rgb Flesh] [0.85 rgb <0.12, 0.04, 0.2>] [1 rgb Flesh] } scale 0.15 }
  #declare Glow = pigment { marble turbulence 0.6 colour_map { [0 rgb 0] [0.8 rgb 0] [0.88 rgb <0.3, 1.2, 3>] [0.96 rgb 0] } scale 0.15 }
#end
#declare Patches = pigment { bozo colour_map { [0 rgb 0.2] [1 rgb 1.8] } scale 0.08 }
#declare Plain = finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { translucency Mfp } }
#declare Fin = finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { translucency Mfp
  #switch (Case)
    #case (2) colour rgb Flesh #break
    #case (3) pigment { Veins } volume_sampling on #break
    #case (4) pigment { Veins } emission pigment { Glow } volume_sampling on #break
    #case (5) colour rgb Flesh thickness pigment { Patches } #break
  #end
} }
#declare Body = union {
  sphere { 0, 0.5 }
  sphere { 0, 1 scale <0.26, 0.34, 0.03> rotate z * -30 translate <0.4, 0.55, 0> }
  cylinder { <0.3, -0.2, 0>, <0.85, -0.05, 0>, 0.05 }
  sphere { <0.85, -0.05, 0>, 0.05 }
}
#if (Case = 6)
  #declare T = texture { pigment { rgb Flesh } finish { Plain } }
#elseif (Case = 1)
  #declare T = texture { pigment { rgb Flesh } finish { Plain } } texture { pigment { rgbt <0.2, 0.45, 0.9, 0.5> } finish { Plain } }
#else
  #declare T = texture { pigment { rgb Dark } finish { Fin } }
#end
object { Body texture { T } interior { ior 1.4 } }
plane { y, -0.5 pigment { rgb 0.35 } }
camera { location <-0.4, 0.4, -3> look_at <0.15, 0.2, 0> angle 44 right x * 4 / 3 }
background { rgb 0.05 }
light_source { <1.5, 2, 6>, rgb 3 }
light_source { <-5, 2, -1>, rgb 0.7 }
light_source { <1, 3, -4>, rgb 0.15 }
