#version 3.7;
// An isosurface alone or as a part of a union: each part keeps its own stated accuracy.
#ifndef (Union) #declare Union = 0; #end
#ifndef (Shadowless) #declare Shadowless = 0; #end
#ifndef (Subsurface) #declare Subsurface = 0; #end
global_settings {
  assumed_gamma 1 mm_per_unit 10
  #if (Subsurface) subsurface { method 2 samples 32, 0 } #end
}
camera { orthographic location <0, 5, -0.001> look_at 0 right x * 2.4 up z * 2.4 }
background { rgb 0 }
#declare Body = isosurface {
  function { x * x + y * y + z * z - 1 }
  contained_by { box { -1.1, 1.1 } } accuracy 0.001 max_gradient 2.2
};
#declare Look = material {
  texture { pigment { rgb <0.9, 0.7, 0.6> }
    finish { ambient 0 diffuse 1 #if (Subsurface) subsurface { translucency <0.8, 0.5, 0.4> } #end } }
  interior { ior 1.33 }
};
#if (Union)
  union { object { Body } sphere { <8, -8, 8>, 0.5 } material { Look } }
#else
  object { Body material { Look } }
#end
sphere { <0, 2.2, 0>, 0.3 no_image
  pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 0 }
  light { pigment { rgb 1 } brightness 4 samples 16 #if (Shadowless) shadowless #end }
}
