// Surface bounce view directions, control: a plain diffuse panel lit from the front re-emits from its front.
// Its exported light does not depend on the viewing direction, so Directions=N (view_directions) must not change the image.
// Mask=1 renders flat region colours for measurement: red the bounce light, green the receivers.
#version 3.7;
#ifndef (Samples) #declare Samples = 16; #end
#ifdef (Directions) #default { light { bounce { view_directions Directions } } } #end
global_settings { assumed_gamma 1 }
camera { location <0, 0.9, -2.6> look_at <0, 0.35, 0.2> angle 50 }
background { rgb 0 }
#ifdef (Mask)
  #declare ReceiverLook = texture { pigment { rgb <0, 1, 0> } finish { ambient 0 diffuse 0 emission 1 } }
#else
  #declare ReceiverLook = texture { pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
#end
light_source { <0, 1.6, -0.6>, rgb 6 spotlight point_at <0, 0.5, 0.15> radius 9 falloff 11 }
box { <-0.45, 0.05, 0.15>, <0.45, 0.95, 0.2>
  #ifdef (Mask)
    pigment { rgb <1, 0, 0> } finish { ambient 0 diffuse 0 emission 1 }
  #else
    pigment { rgb <0.9, 0.75, 0.6> }
    finish { ambient 0 diffuse 1 }
    light { bounce samples Samples }
  #end
}
// receivers: plain diffuse, not lights
box { <-2, -0.02, -2.5>, <2, 0, 1.2> texture { ReceiverLook } }
box { <-1.6, 0, -1.4>, <-1.55, 1.2, 0.6> texture { ReceiverLook } }
box { <1.55, 0, -1.4>, <1.6, 1.2, 0.6> texture { ReceiverLook } }
