// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A candle in a misty room: the flame and its smoke add to the mist, the rising warm air subtracts it and, being thinner, bends light so the brick wall and the print behind ripple. +w360 +h480; Declare=Plume=0 stops the warm air clearing the mist, Smoke=0 drops the smoke, Shimmer=0 the heat.
#version 4.0;
#ifndef (Plume) #declare Plume = 1; #end
#ifndef (Smoke) #declare Smoke = 1; #end
#ifndef (Shimmer) #declare Shimmer = 1; #end
#declare Wick = <0, 1.32, 0>;
#declare WickY = Wick.y;
#declare Back = 1.6;
#declare Mist = 0.04;

global_settings { assumed_gamma 1 }
camera { location <0.5, 1.7, -2.1> look_at <0, 2.1, 0> angle 55 right x * 3 / 4 }
difference {
  box { <-3.1, -0.1, -4.1>, <3.1, 4.1, Back> }
  box { <-3, 0, -4>, <3, 4, Back + 1> }
  box { <-3.2, 1.6, -0.6>, <-2.9, 3.4, 0.8> }
  pigment { rgb <0.62, 0.55, 0.45> }
}
box {
  <-3.1, -0.1, Back>, <3.1, 4.1, Back + 0.1>
  pigment {
    brick pigment { rgb <0.9, 0.86, 0.78> } pigment { granite scale 0.3 color_map { [0 rgb <0.5, 0.15, 0.06>] [1 rgb <0.26, 0.07, 0.03>] } }
    brick_size <0.3, 0.1, 0.15> mortar 0.025
  }
}
box {
  <-3, 3.99, -4>, <3, 4.05, Back>
  pigment { gradient x color_map { [0 rgb 0.1] [0.07 rgb 0.1] [0.07 rgb <0.5, 0.33, 0.18>] [1 rgb <0.42, 0.27, 0.14>] } scale 0.3 }
}
light_source { <-6, 3.4, -3> rgb <0.8, 1, 1.6> spotlight point_at <-1.8, 1.4, Back> radius 9 falloff 13 }
light_source { Wick + y * 0.12 rgb <1.5, 0.85, 0.4> fade_distance 1 fade_power 2 }
box { <-0.9, 0, -0.6>, <0.9, 0.9, 0.6> pigment { rgb <0.3, 0.17, 0.09> } }
union {
  box { <-0.52, -0.52, 0.01>, <0.52, 0.52, 0.04> pigment { rgb <0.12, 0.07, 0.04> } }
  box {
    <-0.45, -0.45, 0>, <0.45, 0.45, 0.01>
    pigment {
      function { 0.25 + 0.5 * abs(mod(floor(atan2(x, y) * 8 / pi + 8), 2) - mod(floor(sqrt(x * x + y * y) / 0.07), 2)) }
      color_map { [0.5 rgb <0.06, 0.1, 0.28>] [0.5 rgb <0.92, 0.85, 0.66>] }
    }
  }
  translate <-0.35, 2.05, Back - 0.04>
}
cylinder { <0, 0.9, 0>, <0, 1.3, 0>, 0.12 pigment { rgb <0.95, 0.9, 0.8> } }
cylinder { <0, 1.28, 0>, Wick, 0.008 pigment { rgb 0.05 } }

box { <-2.99, 0.01, -3.99>, <2.99, 3.98, Back - 0.01> pigment { rgbt 1 } interior { media { scattering { 1, rgb Mist } absorption 0.01 } } }
sphere {
  0, 1 pigment { rgbt 1 }
  interior { media { mix add emission rgb <14, 6, 1.6> density { spherical color_map { [0 rgb 0] [0.4 rgb 0.3] [1 rgb 1] } } } }
  scale <0.06, 0.16, 0.06> translate Wick + y * 0.13
}
#if (Smoke)
  cone {
    Wick + y * 0.2, 0.22, Wick + y * 2.6, 0.6
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        scattering { 1, rgb 6 } absorption rgb <3, 3.1, 3.3>
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
#if (Plume | Shimmer)
  cone {
    Wick, 0.2, <0, 3.95, 0>, 1.4
    pigment { rgbt 1 }
    interior {
      #if (Plume)
        media {
          mix subtract
          scattering { 1, rgb Mist } absorption 0.01
          density {
            function { max(0, 1 - sqrt(x * x + z * z) / (0.08 + 0.3 * (y - WickY))) }
            warp { turbulence 0.25 octaves 4 lambda 2.5 }
            color_map { [0 rgb 0] [0.3 rgb 0.8] [0.6 rgb 1.6] [1 rgb 2] }
          }
        }
      #end
      #if (Shimmer)
        media {
          mix add
          refraction -0.1
          density {
            function { max(0, 1 - sqrt(x * x + z * z) / (0.1 + 0.18 * y)) }
            warp { turbulence <0.2, 0, 0.2> octaves 3 lambda 2 } translate Wick
          }
          density { bozo turbulence 0.6 octaves 3 lambda 2.4 scale <0.07, 0.1, 0.07> color_map { [0 rgb 0] [1 rgb 1] } }
          density { function { max(0, min(1, (y - 0.05) * 8) * (1 - (y - 0.05) / 2.45) * (1 - sqrt(x * x + z * z) / (0.2 + 0.456 * y))) } translate Wick }
        }
      #end
    }
  }
#end
