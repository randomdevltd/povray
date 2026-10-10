// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Glowing fish and jellyfish at night over a seabed of rocks, kelp, sea grass, urchins, a coral fan and a lost anchor, the fish the only light, dimmed and tinted by the water they shine through. +w640 +h480; Declare=Samples=N sets each fish's samples, Murk=0 clears the water of particles.
#version 4.0;
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Murk) #declare Murk = 1; #end
#declare R = seed(2024);

global_settings { assumed_gamma 1 max_trace_level 8 }
camera { location <-0.2, 1.55, -6.6> look_at <0.2, 0.75, 2> angle 62 right x * image_width / image_height }
background { rgb 0 }

box {
  <-26, -2, -16>, <26, 14, 40>
  pigment { rgbt 1 }
  interior {
    ior 1.33
    media {
      absorption rgb <0.42, 0.075, 0.05>
      #if (Murk) scattering { 5, rgb 0.006 eccentricity 0.55 } #end
    }
  }
}

#declare Dunes = function { pattern { bozo turbulence 0.4 octaves 4 lambda 2.2 scale <0.35, 1, 0.25> } }
height_field {
  function 600, 600 { 0.04 + 0.2 * Dunes(x, 0, y) + 0.03 * sin(x * 140 + 6 * Dunes(x * 2, 0, y * 2)) }
  translate <-0.5, 0, -0.5> scale <44, 1.4, 44> translate <0, -0.2, 10>
  texture {
    pigment { granite turbulence 0.3 color_map { [0 rgb <0.58, 0.52, 0.4>] [0.6 rgb <0.5, 0.45, 0.36>] [1 rgb <0.36, 0.32, 0.26>] } scale 0.3 }
    normal { granite 0.25 scale 0.05 }
    finish { diffuse 0.85 }
  }
}

#macro Rock(P, S, Seed)
  #local Q = seed(Seed);
  blob {
    threshold 0.5
    #for (K, 0, 6)
      sphere { <rand(Q) - 0.5, rand(Q) * 0.6, rand(Q) - 0.5> * S * 1.2, S * (0.55 + rand(Q) * 0.4), 1 }
    #end
    texture {
      pigment { granite color_map { [0 rgb <0.32, 0.3, 0.27>] [0.5 rgb <0.22, 0.21, 0.2>] [1 rgb <0.4, 0.36, 0.3>] } scale S * 0.4 }
      normal { crackle 0.6 scale S * 0.15 }
      finish { diffuse 0.8 }
    }
    translate P
  }
#end
Rock(<-3.2, 0, 1.8>, 1.3, 3)
Rock(<-1.6, -0.1, 3.6>, 0.9, 5)
Rock(<2.9, -0.1, 2.6>, 1.6, 7)
Rock(<4.6, 0, 5.5>, 1.2, 11)
Rock(<0.4, -0.1, 5.4>, 0.7, 13)
Rock(<-5.5, 0, 6>, 1.8, 17)
Rock(<1.6, 0, -1.3>, 0.45, 19)
Rock(<-1.1, 0, -2.4>, 0.35, 23)
Rock(<6.5, 0, 0.2>, 1.1, 29)

union {
  #for (K, 0, 15)
    #local B = <-7 + rand(R) * 14, 0, 1 + rand(R) * 9>;
    #local H = 2.5 + rand(R) * 4;
    #local Ph = rand(R) * 6;
    sphere_sweep {
      cubic_spline 10,
      #for (I, -1, 8)
        B + <0.35 * sin(I * 0.7 + Ph), I * H / 7, 0.25 * cos(I * 0.5 + Ph)>, 0.03
      #end
    }
    #for (I, 1, 13)
      #local T = I / 14;
      #local C = B + <0.35 * sin(T * 7 * 0.7 + Ph), T * H, 0.25 * cos(T * 7 * 0.5 + Ph)>;
      sphere { 0, 1 scale <0.4, 0.09, 0.012> translate x * 0.38 rotate <rand(R) * 40 - 20, rand(R) * 360, -25 - rand(R) * 30> translate C }
    #end
  #end
  texture { pigment { rgbf <0.36, 0.3, 0.1, 0.35> } finish { diffuse 0.6, 0.35 } }
}

#declare Patches = array[6] { <-2, 0, -1.4>, <1.2, 0, -0.4>, <-0.8, 0, 1.4>, <3.4, 0, -0.8>, <-4.2, 0, -0.2>, <1.8, 0, 3.8> };
union {
  #for (K, 0, 899)
    #local P = Patches[mod(K, 6)] + <rand(R) - 0.5, 0, rand(R) - 0.5> * 2.4 - y * 0.15;
    #local H = 0.25 + rand(R) * 0.5;
    cone { 0, 0.012, y * H, 0.002 rotate <rand(R) * 40 - 20, 0, rand(R) * 40 - 20> translate P }
  #end
  texture { pigment { rgb <0.25, 0.42, 0.12> } finish { diffuse 0.7, 0.2 } }
}

#macro Urchin(P, S)
  union {
    sphere { 0, S }
    #for (K, 0, 69)
      #local D = vnormalize(<rand(R) - 0.5, rand(R) * 0.8, rand(R) - 0.5>);
      cone { 0, S * 0.08, D * S * 2.6, 0.002 }
    #end
    texture { pigment { rgb <0.14, 0.05, 0.12> } finish { diffuse 0.7 specular 0.3 } }
    translate P + y * S * 0.6
  }
#end
Urchin(<-0.4, 0, 0.2>, 0.16)
Urchin(<0.9, 0, 1.2>, 0.12)
Urchin(<-2.2, 0.3, 0.9>, 0.14)

#macro Fan(P, D, L, Depth)
  #local Q = P + D * L;
  cylinder { P, Q, 0.006 + Depth * 0.004 }
  #if (Depth > 0)
    Fan(Q, vnormalize(vaxis_rotate(D, z, 18 + rand(R) * 14)), L * 0.78, Depth - 1)
    Fan(Q, vnormalize(vaxis_rotate(D, z, -18 - rand(R) * 14)), L * 0.78, Depth - 1)
  #end
#end
union {
  Fan(0, y, 0.42, 6)
  #for (K, 0, 9) cylinder { <-0.9 + K * 0.2, 0.15, 0>, <-0.9 + K * 0.2, 1.9, 0>, 0.004 } #end
  #for (K, 0, 7) cylinder { <-1, 0.3 + K * 0.22, 0>, <1, 0.3 + K * 0.22, 0>, 0.004 } #end
  clipped_by { sphere { y * 0.9, 1.05 } }
  texture { pigment { rgb <0.55, 0.12, 0.08> } finish { diffuse 0.75 } }
  rotate y * 20 translate <1.5, 0, 3.1>
}

union {
  cylinder { <0, 0.12, 0>, <0, 0.12, 2.2>, 0.07 }
  torus { 0.12, 0.04 rotate z * 90 translate <0, 0.12, 2.36> }
  intersection { torus { 0.75, 0.07 } plane { z, 0 } translate <0, 0.12, 0.75> }
  sphere { 0, 1 scale <0.2, 0.05, 0.14> translate <-0.72, 0.12, 0.72> }
  sphere { 0, 1 scale <0.2, 0.05, 0.14> translate <0.72, 0.12, 0.72> }
  cylinder { <-0.9, 0.12, 1.9>, <0.9, 0.12, 1.9>, 0.05 }
  #for (L, 0, 26)
    torus { 0.09, 0.025 scale <1, 1, 1.5> rotate z * 90 * mod(L, 2) rotate y * degrees(atan2(0.1 * cos(0.2 * L), 0.2)) translate <0.5 * sin(0.2 * L), 0.05, 2.6 + 0.2 * L> }
  #end
  texture { pigment { granite color_map { [0 rgb <0.2, 0.09, 0.04>] [1 rgb <0.36, 0.16, 0.06>] } scale 0.1 } normal { granite 0.5 scale 0.05 } finish { diffuse 0.75 } }
  rotate y * -35 translate <-1.6, -0.03, -0.4>
}

#declare FishShape = function {
  max(
    1 - sqrt(pow((x - 0.08) / 0.48, 2) + pow(y / 0.17, 2) + pow(z / 0.09, 2)),
    0.7 * max(0, min(min((-0.3 - x) * 20, (x + 0.62) * 20), 1)) * max(0, 1 - abs(y) / (0.03 + 0.8 * max(0, -0.3 - x))) * max(0, 1 - abs(z) / 0.03),
    0.5 * max(0, min(min((0.18 - x) * 15, (x + 0.12) * 15), 1)) * max(0, 1 - (y - 0.12) / (0.18 - 0.5 * (x + 0.12))) * (y > 0.12) * max(0, 1 - abs(z) / 0.02)
  )
}
#macro Fish(P, Heading, Pitch, S, Colour)
  union {
    box {
      <-0.64, -0.3, -0.12>, <0.62, 0.3, 0.12>
      pigment { rgbt 1 }
      interior {
        ior 1.33
        media {
          mix add method 4 resolution 0.012
          emission Colour * 120
          density { function { max(0, min(1, FishShape(x, y, z))) } }
          light_source { samples Samples }
        }
      }
    }
    sphere { <0.42, 0.04, 0.055>, 0.022 } sphere { <0.42, 0.04, -0.055>, 0.022 }
    texture { pigment { rgb 0.02 } }
    scale S rotate z * Pitch rotate y * Heading translate P
  }
#end
Fish(<-1.4, 0.78, -2.1>, -15, 4, 1.1, rgb <0, 0.55, 1>)
Fish(<0.75, 0.42, -1.1>, 165, -6, 0.85, rgb <1, 0.62, 0.05>)
Fish(<-0.9, 1.35, 2.3>, -25, 8, 1, rgb <0.05, 1, 0.4>)
Fish(<1.05, 1.0, 2.5>, -40, 2, 0.8, rgb <0, 0.55, 1>)
Fish(<0.4, 2.5, 4.6>, -60, 12, 1.2, rgb <0.25, 0.3, 1>)
Fish(<-3.1, 0.95, 3.1>, 10, 0, 0.9, rgb <0.05, 1, 0.4>)
Fish(<-3.8, 1.3, 3.7>, 14, -3, 0.75, rgb <0.05, 1, 0.4>)

#macro Jelly(P, S, Colour)
  union {
    sphere {
      0, 1 pigment { rgbt 1 }
      interior {
        ior 1.33
        media {
          mix add
          emission Colour * 60
          density {
            function { max(0, min(1, (1 - sqrt(x * x + y * y + z * z)) * 4)) * max(0, min(1, (sqrt(x * x + y * y + z * z) - 0.55) * 4 + 0.4)) * max(0, min(1, y * 6 + 0.2)) }
          }
          light_source { samples Samples }
        }
      }
      scale <0.5, 0.42, 0.5>
    }
    union {
      #for (K, 0, 15)
        #local A = K * 22.5 + rand(R) * 10;
        #local L = 1 + rand(R) * 1.4;
        sphere_sweep {
          cubic_spline 7,
          #for (I, -1, 5)
            <0.42 * sin(radians(A)) * (1 - I * 0.08) + 0.06 * sin(I * 1.3 + K), -I * L / 4, 0.42 * cos(radians(A)) * (1 - I * 0.08)>, 0.008
          #end
        }
      #end
      pigment { rgbf <0.8, 0.7, 1, 0.75> }
      finish { diffuse 0.4, 0.3 }
    }
    scale S translate P
  }
#end
Jelly(<2.4, 2.3, 1.6>, 0.8, rgb <0.7, 0.12, 1>)
Jelly(<-4.4, 3.6, 5.6>, 0.7, rgb <0.7, 0.12, 1>)
