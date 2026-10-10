// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Sky lanterns rising over a night sea drift into a cumulus and light it from inside, while lightning and a flash light a storm tower from within and strike the sea. Single scattering only. +w640 +h480; Declare=Samples=N sets each light's samples, Storm=0 drops the lightning tower.
#version 4.0;
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Storm) #declare Storm = 1; #end
#declare R = seed(1907);

global_settings { assumed_gamma 1 max_trace_level 6 }
camera { location <-2, 19, -24> look_at <3, 25, 25> angle 74 right x * image_width / image_height }
sky_sphere {
  pigment { gradient y color_map { [0 rgb <0.006, 0.008, 0.016>] [0.4 rgb <0.001, 0.0015, 0.004>] } }
  pigment { bozo scale 0.0025 color_map { [0 rgbt 1] [0.84 rgbt 1] [0.88 rgbt <0.4, 0.42, 0.5, 0>] [1 rgbt <0.8, 0.8, 0.9, 0>] } }
}
plane {
  y, 0
  pigment { rgb <0.01, 0.015, 0.02> }
  normal { waves 0.25 frequency 0.7 turbulence 0.4 scale <3, 1, 1.5> }
  finish { diffuse 0.3 specular 0.6 roughness 0.002 reflection { 0.02, 0.6 fresnel } conserve_energy }
  interior { ior 1.33 }
}

#declare Puffs = function { pattern { bozo turbulence 0.6 octaves 6 lambda 2.3 omega 0.55 } }
#declare Lobe = function(X, Y, Z, Rx, Ry, Rz) { max(0, 1 - sqrt(X * X / (Rx * Rx) + Y * Y / (Ry * Ry) + Z * Z / (Rz * Rz))) }
#macro Cloud(Low, High, Scale, Body)
  box {
    Low, High
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        scattering { 5, rgb 0.3 eccentricity 0.35 }
        density { function { max(0, min(1, Body(x, y, z) * 3.5 - 0.45 + 0.9 * (Puffs(x / Scale, y / Scale, z / Scale) - 0.5))) } }
      }
    }
  }
#end
#declare Cumulus = function {
  max(Lobe(x + 12, y - 21, z - 11, 11, 6, 9), Lobe(x + 6, y - 19, z - 7, 7, 4.5, 6), Lobe(x + 17, y - 26, z - 15, 7, 6, 6), Lobe(x + 2, y - 17, z - 13, 5, 3.5, 6))
}
#declare Tower = function {
  max(Lobe(x - 18, y - 24, z - 36, 14, 11, 14), Lobe(x - 15, y - 38, z - 34, 10, 9, 10), Lobe(x - 24, y - 16, z - 38, 11, 5, 12), Lobe(x - 9, y - 30, z - 40, 8, 7, 8))
}
#declare Deck = function { Lobe(x * 0.18, y - 14, z - 85, 16, 4.5, 14) }
Cloud(<-27, 12, -2>, <3, 33, 24>, 2.6, Cumulus)
#if (Storm) Cloud(<0, 10, 18>, <36, 50, 54>, 3.4, Tower) #end
Cloud(<-90, 9, 70>, <90, 22, 100>, 5, Deck)

#declare Lift = array[5] { <-6.5, 16.5, -4>, <-9, 19.5, 3>, <-12.5, 22, 9.5>, <-16, 25, 14>, <-2.5, 14, 6> };
#macro Lantern(P, Tilt)
  union {
    difference {
      cone { 0, 0.3, y * 0.85, 0.4 }
      cone { -y * 0.01, 0.29, y * 0.83, 0.39 }
      pigment { rgbf <1, 0.82, 0.55, 0.6> }
      finish { diffuse 0.5, 0.5 }
    }
    torus { 0.3, 0.006 }
    #for (K, 0, 3) cylinder { <0.3 * sin(radians(K * 90)), 0, 0.3 * cos(radians(K * 90))>, y * 0.1, 0.004 } #end
    cylinder { y * 0.07, y * 0.1, 0.035 pigment { rgb 0.1 } }
    texture { pigment { rgb 0.15 } }
    rotate y * rand(R) * 360 rotate <Tilt, 0, Tilt * 0.6>
    translate P
  }
  sphere {
    0, 1 pigment { rgbt 1 }
    interior {
      media {
        mix add method 4 resolution 0.008
        emission rgb <1, 0.6, 0.25> * 20000
        density { spherical color_map { [0 rgb 0] [0.5 rgb 0.3] [1 rgb 1] } }
        light_source { samples Samples }
      }
    }
    scale <0.05, 0.11, 0.05> translate y * 0.2 rotate <Tilt, 0, Tilt * 0.6> translate P
  }
#end
#for (K, 0, 4) Lantern(Lift[K], rand(R) * 16 - 8) #end

#if (Storm)
  #declare Fork = array[64];
  #declare ForkN = 0;
  #macro Jag(A, B, Steps, Rad0, Rad1, Amp)
    #local P = A;
    #for (I, 1, Steps)
      #local Q = A + (B - A) * I / Steps;
      #if (I < Steps) #local Q = Q + <rand(R) - 0.5, (rand(R) - 0.5) * 0.5, rand(R) - 0.5> * Amp; #end
      #local Rad = Rad0 + (Rad1 - Rad0) * I / Steps;
      cylinder { P, Q, Rad } sphere { Q, Rad }
      #if (ForkN < 64) #declare Fork[ForkN] = Q; #declare ForkN = ForkN + 1; #end
      #local P = Q;
    #end
  #end
  merge {
    Jag(<16, 34, 36>, <21, 0, 30>, 18, 0.3, 0.26, 3.4)
    Jag(<5, 27, 40>, <16, 34, 36>, 10, 0.16, 0.3, 3)
    Jag(<16, 34, 36>, <30, 29, 42>, 10, 0.3, 0.14, 3)
    #for (K, 0, 7)
      #local A = Fork[int(rand(R) * ForkN)];
      Jag(A, A + <rand(R) - 0.5, -0.5 - rand(R) * 0.5, rand(R) - 0.5> * 8, 5, 0.12, 0.07, 1.6)
    #end
    pigment { rgbt 1 }
    interior { media { mix add emission rgb <0.72, 0.82, 1> * 5 light_source { samples Samples } } }
  }
  sphere {
    0, 1 pigment { rgbt 1 }
    interior {
      media {
        mix add method 4 resolution 0.25
        emission rgb <0.8, 0.7, 1> * 0.35
        density { spherical turbulence 0.3 color_map { [0 rgb 0] [1 rgb 1] } }
        light_source { samples Samples }
      }
    }
    scale 4.5 translate <22, 38, 40>
  }
#end
