// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Heat shimmer: a long layer of turbulent hot air over a sunlit runway. Hot air is thinner, so its refraction is negative; the turbulence wobbles the distant marker board and the runway edge lines. Declare=Hot=0 for still air. +w640 +h360
#version 4.0;
#ifndef (Hot) #declare Hot = 1; #end

global_settings { assumed_gamma 1 }
camera { location <0, 0.9, -2> look_at <0, 1.4, 60> angle 13 right x * image_width / image_height }
light_source { <-200, 300, -150> rgb 1.3 }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.85, 0.9, 1>] [0.3 rgb <0.35, 0.55, 0.95>] } } }

plane {
  y, 0
  texture {
    pigment { granite scale 0.6 color_map { [0 rgb <0.42, 0.4, 0.37>] [1 rgb <0.5, 0.47, 0.42>] } }
    finish { diffuse 0.7 }
  }
  texture {
    pigment { gradient x color_map { [0 rgbt 1] [0.46 rgbt 1] [0.46 rgb 0.95] [0.54 rgb 0.95] [0.54 rgbt 1] [1 rgbt 1] } scale 3 translate x * 1.5 }
  }
}

#declare Board = union {
  box { <-2.4, 0.2, 0>, <2.4, 3.2, 0.1> pigment { checker rgb 0.05 rgb 0.95 scale 0.4 } }
  cylinder { <-2, 0, 0.12>, <-2, 0.25, 0.12>, 0.08 }
  cylinder { <2, 0, 0.12>, <2, 0.25, 0.12>, 0.08 }
  pigment { rgb 0.3 }
}
object { Board translate z * 60 }
#for (Z, 10, 50, 10) cone { <-4, 0, Z>, 0.25, <-4, 0.7, Z>, 0 pigment { rgb <1, 0.4, 0.05> } } #end

#if (Hot)
  box {
    <-6, -0.5, 1>, <6, 3, 58>
    pigment { rgbt 1 }
    interior {
      media {
        method 3
        refraction -0.00006
        density {
          function { max(0, min(min(1, (z - 1) / 4), min((58 - z) / 4, (6 - abs(x)) / 2)) * max(0, 1 - y / 2.6)) }
        }
        density { bozo turbulence 0.6 octaves 3 lambda 2.2 scale <1.1, 0.3, 2.5> color_map { [0 rgb 0.2] [1 rgb 1.8] } }
      }
    }
  }
#end
