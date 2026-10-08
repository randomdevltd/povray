// Two worlds at the same coordinates: the camera sees "a", an identity-mapped portal shows "b" in the same place.
// Mode 0 portal proof, 1 both worlds untagged and merged, 2 world "a" alone, 3 world "b" alone.
#version 4.0;
#ifndef (Mode) #declare Mode = 0; #end
global_settings { assumed_gamma 1 }

#macro Tags(Name) #if (Mode != 1) tags { Name } #end #end

camera {
  location <0, 1.6, -6>
  look_at <0, 1.1, 0>
  angle 60
  #if (Mode = 0 | Mode = 2) filter_tags { "a" } #end
  #if (Mode = 3) filter_tags { "b" } #end
}

// World "a": warm sky, chequered floor, red spheres, light from the left.
sphere { 0, 200 hollow pigment { rgb <0.95, 0.7, 0.45> } finish { emission 1 diffuse 0 } no_shadow Tags("a") }
plane { y, 0 pigment { checker rgb <0.9, 0.55, 0.2> rgb <0.35, 0.15, 0.05> } Tags("a") }
#for (I, -2, 2)
  sphere { <I * 1.3, 0.6, 1 + mod(I + 2, 2)>, 0.6 pigment { rgb <0.85, 0.1, 0.1> } finish { specular 0.5 } Tags("a") }
#end
light_source { <-6, 8, -4>, rgb <1, 0.9, 0.8> Tags("a") }

// World "b": cool sky, striped floor, blue boxes and a torus, light from the right.
sphere { 0, 200 hollow pigment { rgb <0.35, 0.55, 0.95> } finish { emission 1 diffuse 0 } no_shadow Tags("b") }
plane { y, 0 pigment { gradient x color_map { [0.5 rgb <0.2, 0.3, 0.6>] [0.5 rgb <0.8, 0.85, 0.95>] } } Tags("b") }
#for (I, -1, 1)
  box { <-0.5, 0, -0.5>, <0.5, 1.4, 0.5> rotate 30 * I * y translate <I * 2, 0, 1.5> pigment { rgb <0.1, 0.3, 0.9> } Tags("b") }
#end
torus { 0.8, 0.25 rotate 90 * x translate <0, 1.6, 0.5> pigment { rgb <0.2, 0.9, 0.7> } Tags("b") }
light_source { <6, 8, -4>, rgb <0.8, 0.9, 1> Tags("b") }

#if (Mode = 0)
  // The window between worlds: views through it carry on from the same point, in world "b".
  portal {
    polygon { 5, <-1.4, 0.3>, <-1.4, 2.3>, <1.4, 2.3>, <1.4, 0.3>, <-1.4, 0.3> translate -2 * z }
    to { translate 0 }
    far off
    no_lights
    filter_tags { "b" }
    tags { "a" }
  }
#end
