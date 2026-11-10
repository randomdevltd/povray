// A volume light buried in a subsurface host whose flesh varies or whose texture is layered: Material 0 a uniform flesh,
// 1 a flesh pigment looked up below the surface, 2 two texture layers.
#version 4.0;
#ifndef (Material) #declare Material = 0; #end
#ifndef (Lamp) #declare Lamp = 1; #end
global_settings { assumed_gamma 1 mm_per_unit 1 max_trace_level 8 subsurface { method 2 samples 16, 16 spacing 0.5 } }
camera { location <0, 2.5, -4> look_at 0 angle 40 }
background { rgb 0 }
box { -1, 1
  #switch (Material)
  #case (0)
    pigment { rgb <0.95, 0.85, 0.7> }
    finish { ambient 0 diffuse 0.8 subsurface { translucency <0.8, 0.6, 0.4> colour rgb <0.9, 0.5, 0.4> } }
  #break
  #case (1)
    pigment { rgb <0.95, 0.85, 0.7> }
    finish { ambient 0 diffuse 0.8 subsurface { translucency <0.8, 0.6, 0.4>
      pigment { bozo scale 0.3 colour_map { [0 rgb <0.9, 0.3, 0.2>] [1 rgb 0.95] } } volume_sampling on } }
  #break
  #case (2)
    texture { pigment { rgb <0.9, 0.5, 0.4> } finish { ambient 0 diffuse 0.8 subsurface { translucency 0.5 } } }
    texture { pigment { bozo scale 0.3 colour_map { [0 rgbt <0.95, 0.9, 0.8, 0.2>] [1 rgbt <0.95, 0.9, 0.8, 0.9>] } }
      finish { ambient 0 diffuse 0.8 subsurface { translucency <0.8, 0.6, 0.4> } } }
  #break
  #end
  interior { ior 1.4 }
}
#if (Lamp)
sphere { 0, 0.3 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness 2 samples 8 attenuation { cutoff 0 } } } }
#end
