// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Lightning added inside a storm cloud: the bolt's glow sums with the cloud it runs through, then leaves it below. +w480 +h360; Declare=Seed=N picks another bolt.
#version 4.0;
#ifndef (Seed) #declare Seed = 7; #end
#declare R = seed(Seed);

global_settings { assumed_gamma 1 }
camera { location <0, 3, -22> look_at <0, 5, 0> angle 55 }
background { rgb <0.02, 0.025, 0.04> }
light_source { <20, 60, -20> rgb <0.25, 0.28, 0.35> parallel point_at 0 }
plane { y, 0 pigment { rgb <0.04, 0.05, 0.04> } }

box {
  <-13, 5, -5>, <13, 15, 5>
  pigment { rgbt 1 }
  interior {
    media {
      scattering { 1, rgb 0.1 } absorption rgb <0.04, 0.04, 0.05>
      density { bozo turbulence 0.7 octaves 6 lambda 2.5 scale 2 color_map { [0 rgb 0] [0.35 rgb 0.15] [0.7 rgb 0.8] [1 rgb 1.4] } }
      density {
        spherical turbulence 0.3 octaves 4 scale <11, 3.6, 4> translate y * 10
        color_map { [0 rgb 0] [0.35 rgb 1] [1 rgb 1.2] }
      }
    }
  }
}

#declare P = <-2, 10.5, 0>;
#declare Glow = <0.9, 1, 1.6> * 18;
merge {
  #while (P.y > 0)
    #declare Q = P + <rand(R) * 1.6 - 0.8, -0.35 - rand(R) * 0.6, rand(R) * 1.2 - 0.6>;
    cylinder { P, Q, 0.05 }
    sphere { Q, 0.05 }
    #declare P = Q;
  #end
  pigment { rgbt 1 }
  interior { media { emission Glow } }
}
light_source { <-1.5, 8.8, 0> rgb <1.2, 1.3, 2> * 1.5 fade_distance 2.5 fade_power 2 }
light_source { <-1, 2, 0> rgb <0.8, 0.9, 1.4> fade_distance 3 fade_power 2 }
