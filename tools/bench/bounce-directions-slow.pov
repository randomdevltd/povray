// Surface bounce view directions, expensive per direction: a translucent (method 2) stalk with a light buried
// inside it, so every direction of the stalk's bounce export runs internal-source walks; the floor and wall are
// lit by that bounce alone. Directions=N sets the scene default (#default), which the stalk's bounce block takes.
// Mask=1 renders flat region colours for measurement: red the bounce light, green the receivers.
#version 3.7;
#ifndef (Samples) #declare Samples = 256; #end
#ifdef (Directions) #default { light { bounce { view_directions Directions } } } #end
global_settings { assumed_gamma 1 subsurface { method 2 } }
camera { location <0, 0.5, -1.0> look_at <0, 0.18, 0> angle 45 }
background { rgb 0 }
#ifdef (Mask)
  #declare ReceiverLook = texture { pigment { rgb <0, 1, 0> } finish { ambient 0 diffuse 0 emission 1 } }
#else
  #declare ReceiverLook = texture { pigment { rgb 0.7 } finish { ambient 0 diffuse 1 } }
#end
cylinder { <0, 0, 0>, <0, 0.4, 0>, 0.03
  #ifdef (Mask)
    pigment { rgb <1, 0, 0> } finish { ambient 0 diffuse 0 emission 1 }
  #else
    pigment { rgb <0.8, 0.5, 0.3> }
    finish { ambient 0 diffuse 1 subsurface { translucency <0.2, 0.35, 0.6> } }
    interior { ior 1.4 }
    light { bounce samples Samples }
  #end
}
#ifndef (Mask)
sphere { <0, 0.3, 0>, 0.012 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { ior off light { brightness 100 samples 8 attenuation { cutoff 0 } } } }
#end
// receivers: plain diffuse, not lights
box { <-1.5, -0.02, -1.5>, <1.5, 0, 1.5> texture { ReceiverLook } }
box { <-1.5, 0, 0.3>, <1.5, 1.2, 0.35> texture { ReceiverLook } }
