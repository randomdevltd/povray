// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A candle in a misty room: the flame and its smoke add to the mist, the rising warm air subtracts it, and its heat makes the picture behind shimmer. +w360 +h480; Declare=Plume=0 drops the warm air, Smoke=0 the smoke, Shimmer=0 the heat.
#version 4.0;
#ifndef (Plume) #declare Plume = 1; #end
#ifndef (Smoke) #declare Smoke = 1; #end
#ifndef (Shimmer) #declare Shimmer = 1; #end
#declare Wick = <0, 1.32, 0>;
#declare WickY = Wick.y;

global_settings { assumed_gamma 1 }
camera { location <0.5, 1.7, -2.1> look_at <0, 2.1, 0> angle 55 right x * 3 / 4 }
difference {
  box { <-3.1, -0.1, -4.1>, <3.1, 4.1, 3.1> }
  box { <-3, 0, -4>, <3, 4, 3> }
  box { <-3.2, 1.6, 0.8>, <-2.9, 3.4, 2.2> }
  pigment { rgb <0.32, 0.27, 0.22> }
}
light_source { <-6, 3, 1.5> rgb <0.7, 0.85, 1.4> spotlight point_at <1.5, 2, -0.5> radius 7 falloff 10 }
light_source { Wick + y * 0.12 rgb <1.4, 0.8, 0.35> fade_distance 0.6 fade_power 2 }
box { <-0.9, 0, -0.6>, <0.9, 0.9, 0.6> pigment { rgb <0.3, 0.17, 0.09> } }
union {
  box { <-0.55, 0, -0.03>, <0.55, 1.2, 0> pigment { rgb <0.25, 0.14, 0.06> } }
  box {
    <-0.47, 0.08, -0.04>, <0.47, 1.12, -0.03>
    pigment { radial frequency 14 color_map { [0 rgb <0.8, 0.65, 0.3>] [0.5 rgb <0.8, 0.65, 0.3>] [0.5 rgb <0.15, 0.25, 0.45>] [1 rgb <0.15, 0.25, 0.45>] } rotate x * 90 translate y * 0.6 }
  }
  rotate x * -8 translate <0, 0.9, 0.45>
}
cylinder { <0, 0.9, 0>, <0, 1.3, 0>, 0.12 pigment { rgb <0.95, 0.9, 0.8> } }
cylinder { <0, 1.28, 0>, Wick, 0.008 pigment { rgb 0.05 } }

box { <-3, 0, -4>, <3, 4, 3> pigment { rgbt 1 } interior { media { scattering { 1, rgb 0.1 } absorption 0.01 } } }
sphere {
  0, 1 pigment { rgbt 1 }
  interior { media { mix add emission rgb <14, 6, 1.6> density { spherical color_map { [0 rgb 0] [0.4 rgb 0.3] [1 rgb 1] } } } }
  scale <0.06, 0.16, 0.06> translate Wick + y * 0.13
}
#if (Smoke)
  cylinder {
    Wick + y * 0.2, Wick + y * 2.6, 0.35
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        scattering { 1, rgb 3 } absorption rgb <2.5, 2.6, 2.8>
        density {
          cylindrical turbulence <0.5, 0.05, 0.5> octaves 5 lambda 3 scale <0.1, 1, 0.1>
          color_map { [0 rgb 0] [0.6 rgb 0] [0.9 rgb 0.6] [1 rgb 1] }
          warp { turbulence <0.5, 0, 0.5> octaves 4 lambda 2.2 } scale <1, 0.6, 1>
        }
        density { gradient y color_map { [0 rgb 1] [1 rgb 0] } scale 2.4 translate Wick + y * 0.2 }
      }
    }
  }
#end
#if (Plume)
  cone {
    Wick, 0.2, <0, 3.95, 0>, 1.4
    pigment { rgbt 1 }
    interior {
      media {
        mix subtract
        scattering { 1, rgb 0.1 } absorption 0.01
        density {
          function { max(0, 1 - sqrt(x * x + z * z) / (0.08 + 0.3 * (y - WickY))) }
          warp { turbulence 0.25 octaves 4 lambda 2.5 }
          color_map { [0 rgb 0] [0.3 rgb 0.8] [0.6 rgb 1.6] [1 rgb 2] }
        }
      }
    }
  }
#end
#if (Shimmer)
  cylinder {
    Wick + y * 0.05, Wick + y * 2.4, 0.3
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        refraction -0.04
        density {
          function { max(0, 1 - sqrt(x * x + z * z) / (0.04 + 0.1 * y)) }
          warp { turbulence <0.3, 0.1, 0.3> octaves 4 lambda 2.3 } translate Wick
        }
        density { function { max(0, min(1, (y - 0.05) * 8) * (1 - (y - 0.05) / 2.35) * (1 - sqrt(x * x + z * z) / 0.3)) } translate Wick }
      }
    }
  }
#end
