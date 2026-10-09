// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A thruster plume on a test stand: an emitting exhaust core inside a wider sheath of hot gas whose negative refraction warps the hangar wall behind it. Declare=Bend=0 drops the refraction. +w640 +h400
#version 4.0;
#ifndef (Bend) #declare Bend = 1; #end

global_settings { assumed_gamma 1 }
camera { location <-1.5, 1.2, -8.5> look_at <0.6, 0.1, 0> angle 40 right x * image_width / image_height }
light_source { <-6, 8, -10> rgb 0.7 }
light_source { <2, 0, 0> rgb <1, 0.8, 0.6> fade_distance 2 fade_power 2 }

box { <-20, -5, 4>, <20, 10, 4.2> pigment { checker rgb 0.55 rgb 0.3 scale 0.5 } finish { diffuse 0.8 } }
plane { y, -2.5 pigment { rgb 0.25 } finish { reflection 0.1 } }
box { <-5, -2.5, -1.2>, <-3.2, 1.6, 1.2> pigment { rgb <0.35, 0.37, 0.4> } }

#declare Axis = <1, -0.08, 0>;
union {
  cylinder { <-3.2, 0, 0>, <-2.2, 0, 0>, 0.45 }
  difference {
    cone { <-2.2, 0, 0>, 0.3, <-0.8, 0, 0>, 0.62 }
    cone { <-2.21, 0, 0>, 0.26, <-0.79, 0, 0>, 0.58 }
  }
  pigment { rgb <0.5, 0.42, 0.38> } finish { specular 0.5 metallic }
  rotate z * -4.6
}

#declare Plume = function(x, y, z) { exp(-(y * y + z * z) / (0.09 * (1 + x * 0.35) * (1 + x * 0.35))) * exp(-x / 5) * min(1, x * 8) }
cylinder {
  <-0.8, 0, 0>, <9, 0, 0>, 2.2
  pigment { rgbt 1 }
  interior {
    media {
      mix add
      method 3 intervals 1 samples 16
      emission rgb 0.45
      density {
        function { Plume(x + 0.8, y, z) * (0.75 + 0.25 * cos(14 * (x + 0.8))) }
        color_map { [0 rgb 0] [0.25 rgb <1.2, 0.45, 0.15>] [0.6 rgb <1.4, 1.1, 0.9>] [1 rgb <1.6, 1.8, 2.6>] }
      }
    }
    #if (Bend)
      media {
        method 3
        refraction -0.08
        density {
          function { exp(-(y * y + z * z) / (0.5 * (1 + (x + 0.8) * 0.3))) * exp(-(x + 0.8) / 6) }
          warp { turbulence <0.3, 0.15, 0.15> octaves 3 }
        }
        density { function { max(0, min(1, (x + 0.8) * 4) * min(1, (9 - x) / 2) * (1 - sqrt(y * y + z * z) / 2.2)) } }
      }
    #end
  }
  rotate z * -4.6
}
