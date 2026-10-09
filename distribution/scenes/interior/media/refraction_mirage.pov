// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// An inferior mirage: air over a hot desert road is thinnest at the ground, so grazing views curve up and the road shows the sky and the posts upside down beneath them. The camera stands inside the refracting air. +w640 +h300
#version 4.0;
#ifndef (Hot) #declare Hot = 1; #end

global_settings { assumed_gamma 1 refraction_angle 1 }
camera { location <0, 0.75, 0> look_at <0, 0.62, 100> angle 22 right x * image_width / image_height }
light_source { <300, 400, -200> rgb 1.3 }
sky_sphere {
  pigment { gradient y color_map { [0 rgb <0.9, 0.93, 1>] [0.15 rgb <0.45, 0.62, 0.95>] [0.4 rgb <0.2, 0.38, 0.85>] } }
}

plane { y, 0 pigment { granite scale 3 color_map { [0 rgb <0.78, 0.62, 0.42>] [1 rgb <0.88, 0.74, 0.55>] } } }
box {
  <-1.6, -0.01, -1>, <1.6, 0.002, 400>
  pigment { gradient x color_map { [0 rgb 0.16] [0.485 rgb 0.16] [0.485 rgb <0.9, 0.8, 0.2>] [0.515 rgb <0.9, 0.8, 0.2>] [0.515 rgb 0.16] [1 rgb 0.16] } scale 3.2 translate x * -1.6 }
}
#for (Z, 20, 380, 20)
  cylinder { <2.2, 0, Z>, <2.2, 1.2, Z>, 0.06 pigment { rgb <0.9, 0.9, 0.85> } }
  box { <2.15, 1.05, Z - 0.02>, <2.25, 1.2, Z + 0.02> pigment { rgb <0.9, 0.1, 0.05> } }
#end
#for (I, 0, 7)
  sphere { <-60 + I * 21, -6, 300 + mod(I * 37, 50)>, 12 scale <1, 0.6, 1> pigment { rgb <0.55, 0.42, 0.32> } }
#end

#if (Hot)
  box {
    <-80, -0.5, -10>, <80, 3, 420>
    pigment { rgbt 1 }
    interior {
      media {
        method 3
        refraction -0.0007
        density {
          function { exp(-max(y, 0) / 0.18) * max(0, min(min(1, (z + 10) / 8), min((420 - z) / 30, (80 - abs(x)) / 20))) }
        }
      }
    }
  }
#end
