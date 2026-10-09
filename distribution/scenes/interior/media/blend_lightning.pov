// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Lightning in a storm cloud: the bolt and its glow add, and a multiply volume tints the grey cloud blue along crackles from the strike. +w480 +h360; Declare=Tint=0 drops the multiply, Declare=Seed=N picks another bolt.
#version 4.0;
#ifndef (Seed) #declare Seed = 11; #end
#ifndef (Tint) #declare Tint = 1; #end
#declare R = seed(Seed);

global_settings { assumed_gamma 1 }
camera { location <0, 3.5, -24> look_at <0, 6, 0> angle 58 }
background { rgb <0.004, 0.006, 0.012> }
light_source { <-15, 40, 80> rgb <0.3, 0.34, 0.45> parallel point_at <0, 8, 0> media_attenuation on }
plane { y, 0 pigment { rgb <0.03, 0.035, 0.03> } normal { granite 0.2 scale 2 } }

box {
  <-15, 5.5, -6>, <15, 15, 6>
  pigment { rgbt 1 }
  interior {
    media {
      scattering { 2, rgb 0.05 } absorption rgb 0.09
      density { bozo turbulence 0.8 octaves 6 lambda 2.5 scale 2.2 color_map { [0 rgb 0] [0.35 rgb 0.1] [0.7 rgb 0.9] [1 rgb 1.5] } }
      density { spherical turbulence 0.3 octaves 4 scale <13, 3.8, 5> translate y * 10 color_map { [0 rgb 0] [0.3 rgb 1] [1 rgb 1.2] } }
    }
  }
}

#declare J = <-0.5, 7.4, 0>;
#declare F = <0.4, 3.0, 0.3>;
#declare Glow = array[400];
#declare GlowN = 0;
#macro Jag(A, B, Steps, Rad0, Rad1, Amp, Glows)
  #local P = A;
  #for (I, 1, Steps)
    #local Q = A + (B - A) * I / Steps;
    #if (I < Steps) #local Q = Q + <rand(R) - 0.5, (rand(R) - 0.5) * 0.4, rand(R) - 0.5> * Amp; #end
    #local Rad = Rad0 + (Rad1 - Rad0) * I / Steps;
    cylinder { P, Q, Rad } sphere { Q, Rad }
    #if (Glows) #declare Glow[GlowN] = Q; #declare GlowN = GlowN + 1; #end
    #declare Last[I] = Q;
    #local P = Q;
  #end
#end
#macro Fronds(Count, Steps, Down, Len, Rad)
  #for (K, 1, Count)
    #local A = Last[1 + int(rand(R) * (Steps - 1))];
    #local Dir = vnormalize(<rand(R) - 0.5, -Down - rand(R) * 0.5, rand(R) - 0.5>);
    Jag(A, A + Dir * Len * (0.6 + rand(R) * 0.6), 4, Rad, Rad * 0.3, Len * 0.3, 0)
  #end
#end
#declare Last = array[32];
#declare Ground = array[3] { <-4.2, 0, -0.6>, <3.4, 0, 0.9>, <0.9, 0, -1.4> };
merge {
  #for (K, 0, 2)
    Jag(<-4.5 + K * 3.8 + rand(R), 10.4 + rand(R) * 1.2, rand(R) - 0.5>, J, 7, 0.018, 0.032, 0.9, 1)
    Fronds(2, 7, -0.2, 1.4, 0.012)
  #end
  Jag(J, F, 10, 0.075, 0.06, 0.7, 1)
  Fronds(3, 10, 0.4, 1.6, 0.02)
  #for (K, 0, 2)
    #local W = array[3] { 0.055, 0.035, 0.024 };
    Jag(F, Ground[K], 9, W[K], W[K] * 0.7, 0.8, 1)
    Fronds(2 + K, 9, 0.5, 1.2, W[K] * 0.4)
  #end
  pigment { rgbt 1 }
  interior { media { mix add emission rgb <0.9, 1, 1.8> * 30 } }
}
#for (I, 0, GlowN - 1)
  sphere {
    0, 1 pigment { rgbt 1 }
    interior { media { mix add emission rgb <0.25, 0.35, 1> * 0.8 density { spherical color_map { [0 rgb 0] [1 rgb 1] } } } }
    scale 0.45 + rand(R) * 0.25 translate Glow[I]
  }
#end

#for (K, 0, 2)
  sphere {
    0, 1 pigment { rgbt 1 }
    interior {
      media {
        mix add
        scattering { 1, rgb 0.5 } absorption 0.4 emission rgb <0.15, 0.3, 1.2>
        density { bozo turbulence 0.6 octaves 5 scale 0.3 color_map { [0 rgb 0] [0.5 rgb 0.3] [1 rgb 1.2] } }
        density { spherical turbulence 0.4 color_map { [0 rgb 0] [0.4 rgb 1] [1 rgb 1] } }
        density { gradient y color_map { [0 rgb 1] [1 rgb 0] } scale 2 translate -y }
      }
    }
    scale <0.7, 1.5, 0.7> translate Ground[K] + y * 0.6
  }
  light_source { Ground[K] + y * 0.25 rgb <0.4, 0.6, 1.6> fade_distance 1.5 fade_power 2 media_interaction off }
#end
light_source { J rgb <1.2, 1.3, 2> * 1.1 fade_distance 3 fade_power 2 }
light_source { F rgb <0.8, 0.9, 1.5> fade_distance 3 fade_power 2 media_interaction off }

#if (Tint)
  #declare Crack = function { pattern { crackle turbulence 0.35 octaves 4 scale 0.22 } }
  sphere {
    0, 1 pigment { rgbt 1 }
    interior {
      media {
        mix multiply
        density {
          function {
            min(1, max(0, 1 - Crack(x, y, z) / 0.05) * max(0, 1 - 1.1 * sqrt(x * x + y * y + z * z)) * 1.6
                   + 0.35 * max(0, 1 - sqrt(x * x + y * y + z * z)))
          }
          color_map { [0 rgb 1] [0.3 rgb <0.6, 1.0, 3>] [1 rgb <1.5, 3.5, 14>] }
        }
      }
    }
    scale <6, 4, 4> translate J
  }
#end
