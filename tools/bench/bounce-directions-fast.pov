// Surface bounce view directions, cheap per direction: an optically thin translucent (method 2) globe with a
// light buried inside it. Its bounce export's light depends on the viewing direction, and with the material
// thin each direction's internal-source walks are short. The floor and wall are lit by that bounce alone.
// Directions=N sets the globe's own bounce directions; Cells=N its point budget.
// Mask=1 renders flat region colours for measurement: red the bounce light, green the receivers.
#version 3.7;
#ifndef (Samples) #declare Samples = 256; #end
global_settings { assumed_gamma 1 subsurface { method 2 } }
camera { location <0, 0.5, -1.0> look_at <0, 0.15, 0> angle 45 }
background { rgb 0 }
#ifdef (Mask)
  #declare ReceiverLook = texture { pigment { rgb <0, 1, 0> } finish { ambient 0 diffuse 0 emission 1 } }
#else
  #declare ReceiverLook = texture { pigment { rgb 0.7 } finish { ambient 0 diffuse 1 } }
#end
sphere { <0, 0.12, 0>, 0.1
  #ifdef (Mask)
    pigment { rgb <1, 0, 0> } finish { ambient 0 diffuse 0 emission 1 }
  #else
    pigment { rgb <0.8, 0.6, 0.4> }
    finish { ambient 0 diffuse 1 subsurface { translucency <8, 6, 4> } }
    interior { ior 1.4 }
    light { bounce
      #ifdef (Directions) { view_directions Directions } #end
      #ifdef (Cells) domain_cells Cells #end
      samples Samples }
  #end
}
#ifndef (Mask)
sphere { <0, 0.12, 0>, 0.02 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { ior off light { brightness 20 samples 8 attenuation { cutoff 0 } } } }
#end
// receivers: plain diffuse, not lights
box { <-1.5, -0.02, -1.5>, <1.5, 0, 1.5> texture { ReceiverLook } }
box { <-1.5, 0, 0.3>, <1.5, 1.2, 0.35> texture { ReceiverLook } }
