// Shadow edges on subsurface light at its worst for the point cloud: a grid of thin rods over a long-diffusion slab and
// cylinder, lit by a small area light and a point light; Shape 0 both, 1 the slab, 2 the cylinder. See doc/PERF.md.
#version 3.7;
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 21; #end
#ifndef (Method) #declare Method = 1; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
#ifndef (Shape) #declare Shape = 0; #end
#ifndef (Mfp) #declare Mfp = 0.2; #end
#ifndef (Albedo) #declare Albedo = 0.95; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 10
  #if (Subsurface) subsurface { samples Diffuse, Single method Method spacing Spacing } #end
}

camera { perspective location <0, 2.3, -4.3> look_at <0, 0.25, 0.2> angle 46 right x * 4 / 3 }

light_source {
  <-1.2, 7, -0.6>, rgb <1.0, 0.92, 0.82> * 0.75
  area_light <0.12, 0, 0>, <0, 0, 0.12>, 4, 4
  adaptive 1 circular orient
}
light_source { <2.6, 6, 1.8>, rgb <0.75, 0.85, 1.0> * 0.35 }

plane { y, -0.01 pigment { rgb 0.25 } }

#declare Rod = texture { pigment { rgb 0.15 } finish { diffuse 0.6 } }
union {
  #for (I, -12, 12)
    cylinder { <I * 0.2, 3, -3>, <I * 0.2, 3, 3>, 0.035 }
  #end
  #for (I, -7, 7)
    cylinder { <-3, 3.15, I * 0.36>, <3, 3.15, I * 0.36>, 0.025 }
  #end
  texture { Rod }
}

#declare Glow = texture {
  pigment { rgb Albedo }
  finish { diffuse 1 specular 0.2 roughness 0.05 subsurface { translucency Mfp } }
}
#if (Shape != 2)
  box { <-1.9, 0, -1.3>, <1.9, 0.5, 0.45> texture { Glow } interior { ior 1.4 } }
#end
#if (Shape != 1)
  cylinder { <-1.6, 0.42, 1.05>, <1.6, 0.42, 1.05>, 0.42 texture { Glow } interior { ior 1.4 } }
#end
