#version 3.8;
// Fields prepared after the parse from patterns that test an object: Field=1 a varying refraction, Field=2 an interior
// light's pigment. prepared_fields_threads.sh requires the same image on one thread and on six.
#ifndef (Field) #declare Field = 1; #end
global_settings { assumed_gamma 1 }
#declare Blob = blob { threshold 0.5 sphere { <-0.5, 0, 0>, 1.2, 1 } sphere { <0.6, 0.3, 0>, 1.0, 1 } }
#declare Tested = function { pattern { potential { Blob } } }
#if (Field = 1)
  camera { orthographic location <0, 0, -10> look_at 0 right x * 4 up y * 3 }
  plane { z, 4 pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 0.5 } finish { ambient 0 emission 1 diffuse 0 } }
  box { <-2, -1.5, -1>, <2, 1.5, 1> pigment { rgbt 1 }
    interior { ior 1 refraction { detail 0.05 function { 0.02 * Tested(x, y, z) } } } }
#else
  camera { location <0, 1, -5> look_at 0 angle 50 }
  plane { y, -1.3 pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
  sphere { 0, 1.2 hollow no_image pigment { rgbt 1 }
    interior { light { brightness 2 samples 4 pigment { potential { Blob } color_map { [0 rgb 0.1] [1 rgb 1] } } } } }
#end
