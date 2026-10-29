// sslt-bounce-shell.pov: a thin subsurface bowl whose bounce light falls on a floor. Case 0: point lights and an emitting
// sphere in the cavity; 1: point lights in the floor's plane only; 2: one light in the cavity; 3: a union of two materials.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Bounce) #declare Bounce = 1; #end
global_settings { assumed_gamma 1 max_trace_level 16 mm_per_unit 10 subsurface { method 2 samples 8, 2 } }
camera { location <0, 2.4, -1.4> look_at <0, 0, 0.1> angle 70 right x * image_width / image_height }
background { rgb 0 }
#if (Case = 0)
  light_source { <-2, 4, -3>, rgb <1, 0.95, 0.9> * 2 }
  light_source { <3, 1.5, 1>, rgb <0.5, 0.6, 1> }
  sphere { <0, 0.35, 0>, 0.12 pigment { rgb <1, 0.5, 0.2> } finish { ambient 0 emission 6 diffuse 0 } light { emission samples Samples } }
#end
#if (Case = 1)
  light_source { <-3, 0, -3>, rgb <1, 0.95, 0.9> * 4 }
  light_source { <3, 0, 1>, rgb <0.5, 0.6, 1> * 2 }
#end
#if (Case = 2)
  light_source { <0, 0.4, 0>, rgb <1, 0.9, 0.8> }
#end
#if (Case = 3)
  light_source { <-2, 4, -3>, rgb <1, 0.95, 0.9> * 2 }
  light_source { <3, 1.5, 1>, rgb <0.5, 0.6, 1> }
#end
plane { y, 0 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
#if (Case = 3)
union {
  sphere { <-0.28, 0.3, 0>, 0.3
    texture { pigment { rgb <0.9, 0.6, 0.5> } finish { ambient 0 diffuse 0.6 subsurface { translucency <0.4, 0.2, 0.1> } } }
    interior { ior 1.36 }
  }
  sphere { <0.28, 0.3, 0>, 0.3
    texture { pigment { rgb <0.5, 0.7, 0.9> } finish { ambient 0 diffuse 0.6 subsurface { translucency <0.05, 0.1, 0.2> } } }
    interior { ior 1.5 }
  }
  box { <-0.06, 0, -0.5>, <0.06, 0.12, 0.5> pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 } }
  #if (Bounce) light { emission bounce { passes 1 } samples Samples } #end
  no_image
}
#else
difference {
  intersection { sphere { 0, 1 scale <0.5, 0.42, 0.5> } plane { -y, 0 } }
  sphere { 0, 1 scale <0.475, 0.395, 0.475> }
  texture {
    pigment { rgbf <0.72, 0.55, 0.9, 0.55> }
    finish { ambient 0 diffuse 0.35, 0.35 specular 0.45 roughness 0.025 subsurface { translucency <0.08, 0.05, 0.1> } }
  }
  interior { ior 1.36 }
  #if (Bounce) light { emission bounce { passes 1 } samples Samples } #end
  no_image
  scale 0.9 translate <0, 0.25, 0>
}
#end
