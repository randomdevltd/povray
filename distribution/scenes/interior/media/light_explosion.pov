// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A fireball in a night street, red outside through orange to a white core, lights the buildings, a wrecked car, lamp posts, wires and flying debris, which throw their shadows back across the road; its own smoke billows over it, shading its light, and its heat warps the street behind. +w640 +h480; Declare=Samples=N sets the fire's samples, Refract=0 stops the heat haze, Smoke=0 drops the smoke plume and dust.
#version 4.0;
#ifndef (Samples) #declare Samples = 24; #end
#ifndef (Smoke) #declare Smoke = 1; #end
#ifndef (Refract) #declare Refract = 1; #end
#declare R = seed(1883);
#declare F = <0, 4.2, 18>;

global_settings { assumed_gamma 1 max_trace_level 6 }
camera { location <3.2, 1.6, -7> look_at <-0.4, 5.6, 18> angle 74 right x * image_width / image_height }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.035, 0.04, 0.075>] [0.3 rgb <0.006, 0.008, 0.02>] } } }

#declare Brick = texture {
  pigment { brick rgb <0.55, 0.52, 0.48>, rgb <0.42, 0.2, 0.12> brick_size <0.25, 0.08, 0.12> mortar 0.012 }
  normal { granite 0.15 scale 0.1 }
  finish { diffuse 0.85 }
}
#declare Plaster = texture { pigment { granite scale 2 color_map { [0 rgb <0.62, 0.58, 0.5>] [1 rgb <0.48, 0.44, 0.38>] } } normal { granite 0.08 scale 0.2 } finish { diffuse 0.85 } }
#declare Metal = texture { pigment { rgb 0.18 } finish { diffuse 0.6 specular 0.5 roughness 0.01 } }
#declare Rubble = texture { pigment { granite scale 0.3 color_map { [0 rgb 0.38] [0.5 rgb <0.4, 0.3, 0.24>] [1 rgb 0.22] } } normal { granite 0.5 scale 0.1 } finish { diffuse 0.85 } }

difference {
  box { <-30, -1, -20>, <30, 0, 60> }
  sphere { F * <1, 0, 1> + y * 2.2, 3 }
  texture {
    pigment { granite scale 0.4 color_map { [0 rgb 0.3] [1 rgb 0.2] } }
    normal { granite 0.25 scale 0.05 }
    finish { diffuse 0.85 specular 0.1 }
  }
}
torus { 3.2, 0.35 scale <1, 0.6, 1> translate F * <1, 0, 1> texture { Rubble } }
union {
  box { <-8, 0, -20>, <-6, 0.18, 60> }
  box { <6, 0, -20>, <8, 0.18, 60> }
  #for (Z, -20, 58, 6) box { <-5.6, 0, Z>, <-5.4, 0.004, Z + 3> } box { <5.4, 0, Z>, <5.6, 0.004, Z + 3> } #end
  texture { pigment { rgb 0.4 } normal { granite 0.2 scale 0.05 } finish { diffuse 0.85 } }
}

#macro Building(X0, X1, Z0, Z1, H, Face, Tex, Seed)
  #local Q = seed(Seed);
  #local Floors = int((H - 0.8) / 3.2);
  #local Bays = int((Z1 - Z0) / 2.4);
  difference {
    box { <X0, 0, Z0>, <X1, H, Z1> }
    #for (Fl, 0, Floors - 1)
      #for (B, 0, Bays - 1)
        #local Zc = Z0 + (B + 0.5) * (Z1 - Z0) / Bays;
        box { <Face - 0.3, 1 + Fl * 3.2, Zc - 0.55>, <Face + 0.3, 2.9 + Fl * 3.2 - (Fl = 0) * 0.3, Zc + 0.55> }
      #end
    #end
    texture { Tex }
  }
  #local S = (Face > 0 ? -1 : 1);
  union {
    #for (Fl, 0, Floors - 1)
      #for (B, 0, Bays - 1)
        #local Zc = Z0 + (B + 0.5) * (Z1 - Z0) / Bays;
        box { <Face - 0.25 * S, 0.92 + Fl * 3.2, Zc - 0.62>, <Face + 0.1 * S, 1.0 + Fl * 3.2, Zc + 0.62> }
        box { <Face - 0.2 * S, 1 + Fl * 3.2, Zc - 0.03>, <Face - 0.17 * S, 2.9 + Fl * 3.2, Zc + 0.03> }
        #if (Fl > 0 & rand(Q) > 0.7)
          box { <Face, 0.95 + Fl * 3.2, Zc - 0.8>, <Face + 0.9 * S, 1.05 + Fl * 3.2, Zc + 0.8> }
          #for (K, 0, 12) cylinder { <Face + 0.85 * S, 1.05 + Fl * 3.2, Zc - 0.75 + K * 0.125>, <Face + 0.85 * S, 2 + Fl * 3.2, Zc - 0.75 + K * 0.125>, 0.012 } #end
          cylinder { <Face + 0.85 * S, 2 + Fl * 3.2, Zc - 0.8>, <Face + 0.85 * S, 2 + Fl * 3.2, Zc + 0.8>, 0.025 }
        #end
      #end
    #end
    box { <Face - 0.1 * S, H, Z0>, <Face + 0.4 * S, H + 0.35, Z1> }
    texture { Metal }
  }
  box { <Face - 0.25 * S, 1.1, Z0>, <Face - 0.2 * S, H - 0.2, Z1> texture { pigment { rgb 0.01 } } }
#end
Building(-18, -8, -12, -1, 13, -8, Brick, 1)
Building(-18, -8, 0, 9, 16.5, -8, Plaster, 2)
Building(-18, -8, 10, 21, 10, -8, Brick, 3)
Building(-18, -8, 22, 34, 19.5, -8, Plaster, 4)
Building(8, 18, -12, -2, 10, 8, Plaster, 5)
Building(8, 18, -1, 10, 13, 8, Brick, 6)
Building(8, 18, 11, 23, 16.5, 8, Plaster, 7)
Building(8, 18, 24, 34, 13, 8, Brick, 8)
difference {
  box { <-18, 0, 40>, <18, 22, 50> }
  #for (Fl, 0, 5) #for (B, -6, 5) box { <B * 2.8 + 0.6, 1 + Fl * 3.4, 39.7>, <B * 2.8 + 2.2, 3 + Fl * 3.4, 40.3> } #end #end
  texture { Plaster }
}

#macro Lamp(P, S)
  union {
    cylinder { 0, y * 5.5, 0.07 }
    cylinder { y * 5.5, <-1.2 * S, 5.6, 0>, 0.045 }
    box { <-1.4 * S - 0.15, 5.45, -0.12>, <-1.2 * S + 0.15, 5.6, 0.12> }
    texture { Metal }
    translate P
  }
#end
#for (Z, -6, 34, 8) Lamp(<-6.5, 0.18, Z>, -1) Lamp(<6.5, 0.18, Z + 4>, 1) #end
union {
  #for (K, 0, 2)
    #local Z = 4 + K * 16;
    cylinder { <-7.5, 0, Z>, <-7.5, 9, Z>, 0.12 }
    cylinder { <7.5, 0, Z + 1>, <7.5, 9, Z + 1>, 0.12 }
    box { <-8.2, 8.4, Z - 0.06>, <-6.8, 8.5, Z + 0.06> }
    #for (W, 0, 2)
      sphere_sweep { cubic_spline 5, <-9, 8.8 - W * 0.2, Z - 2>, 0.015, <-7.5, 8.5 - W * 0.2, Z>, 0.015, <0, 7.5 - W * 0.2, Z + 0.5>, 0.015, <7.5, 8.5 - W * 0.2, Z + 1>, 0.015, <9, 8.8 - W * 0.2, Z + 3>, 0.015 }
      sphere_sweep { cubic_spline 5, <-7.5, 8.5 - W * 0.2, Z - 16>, 0.012, <-7.5, 8.5 - W * 0.2, Z>, 0.012, <-7.5, 7.4 - W * 0.2, Z + 8>, 0.012, <-7.5, 8.5 - W * 0.2, Z + 16>, 0.012, <-7.5, 8.5 - W * 0.2, Z + 32>, 0.012 }
    #end
  #end
  texture { pigment { rgb <0.12, 0.09, 0.07> } finish { diffuse 0.8 } }
}

union {
  box { <-2.1, 0.35, -0.9>, <2.1, 1.1, 0.9> }
  box { <-1.1, 1.1, -0.8>, <0.9, 1.7, 0.8> }
  #for (I, -1, 1, 2) #for (J, -1, 1, 2) cylinder { <I * 1.35, 0.35, J * 0.95>, <I * 1.35, 0.35, J * 0.75>, 0.35 } #end #end
  texture { pigment { rgb <0.22, 0.25, 0.3> } finish { diffuse 0.7 specular 0.6 roughness 0.005 reflection 0.06 } }
  rotate z * 160 translate <0, 1.75, 0> rotate y * 70 translate <-3.4, 0, 9.5>
}
union {
  #for (K, 0, 5)
    #local P = <-4.2 + rand(R) * 9, 0, 3 + rand(R) * 8>;
    #if (rand(R) > 0.5)
      cylinder { 0, y * 0.9, 0.3 translate P }
    #else
      cylinder { 0, y * 0.9, 0.3 rotate z * 90 rotate y * rand(R) * 180 translate P + y * 0.3 }
    #end
  #end
  texture { pigment { rgb <0.5, 0.12, 0.05> } finish { diffuse 0.7 specular 0.3 } }
}

#declare Chunk = intersection {
  #for (K, 0, 5) plane { vnormalize(<rand(R) - 0.5, rand(R) - 0.5, rand(R) - 0.5>), 0.5 + rand(R) * 0.2 } #end
  box { -0.8, 0.8 }
}
union {
  #for (K, 0, 119)
    #local D = vnormalize(<rand(R) - 0.5, rand(R) * 0.9 - 0.15, rand(R) - 0.5>);
    #local P = F + D * (4.6 + pow(rand(R), 0.7) * 8);
    #if (P.y > 0.3)
      object { Chunk scale 0.1 + pow(rand(R), 2.5) * 0.5 rotate <rand(R), rand(R), rand(R)> * 360 translate P }
    #end
  #end
  #for (K, 0, 199)
    #local A = rand(R) * 2 * pi;
    #local D = 3.8 + pow(rand(R), 1.6) * 12;
    object { Chunk scale 0.05 + pow(rand(R), 3) * 0.3 rotate <rand(R), rand(R), rand(R)> * 360 translate F * <1, 0, 1> + <D * cos(A), 0.02, D * sin(A)> }
  #end
  texture { Rubble }
}

#declare Puff = function { pattern { bozo turbulence 0.55 octaves 6 lambda 2.4 omega 0.55 scale 0.9 } }
#declare Billow = function { pattern { wrinkles scale 1.3 } }
#declare Rise = function { pattern { bozo turbulence <0.4, 0.15, 0.4> octaves 4 lambda 2.2 scale <0.06, 0.12, 0.06> } }
#declare Ball = function(Px, Py, Pz, Rad, Lob) { max(0, min(1, (1 - sqrt(Px * Px + Py * Py + Pz * Pz) / Rad + Lob * (Puff(Px, Py, Pz) - 0.5)) * 3)) }
#declare PlumeR = function(Hy) { 3.2 + 0.28 * max(0, Hy) }
#declare Plume = function(Px, Py, Pz) {
  max(0, min(1,
    min(1, max(0, 1.4 * (1 - sqrt(pow(Px - 0.04 * Py, 2) + pow(Pz - 0.02 * Py, 2)) / PlumeR(Py))))
    * min(1, max(0, (Py - 1) / 3)) * min(1, max(0, (31 - Py) / 5)) * min(1, max(0, (sqrt(Px * Px + Py * Py + Pz * Pz) - 2.5) / 2))
    * (0.15 + 1.3 * pow(Puff(Px * 0.45, Py * 0.4, Pz * 0.45), 2))
  ))
}

#if (Refract)
  sphere {
    0, 1 pigment { rgbt 1 }
    interior { media { refraction -0.008 density { function { pow(max(0, 1 - x * x - y * y - z * z), 2) * (0.3 + 0.7 * Rise(x, y, z)) } } } }
    scale <15, 20, 13> translate F + y * 8
  }
#end

sphere {
  0, 5.6 pigment { rgbt 1 }
  interior {
    media {
      mix add
      emission rgb <1, 0.08, 0.01> * 1.5
      density { function { Ball(x, y, z, 4.8, 1.1) * (0.12 + 0.88 * pow(Puff(x * 1.7, y * 1.7, z * 1.7), 2)) } }
      light_source { samples Samples }
    }
    media {
      emission rgb <1, 0.36, 0.04> * 2
      density { function { Ball(x, y, z, 3.4, 0.9) * (0.3 + 0.7 * Puff(x * 1.4, y * 1.4, z * 1.4)) } }
      light_source { samples Samples }
    }
    media {
      emission rgb <1, 0.78, 0.4> * 5
      density { function { Ball(x * 1.1, y, z * 1.1, 2, 0.6) } }
      light_source { samples Samples }
    }
    media {
      absorption rgb <1.8, 1.85, 1.95> scattering { 1, rgb 0.6 }
      density { function { max(0, min(1, (sqrt(x * x + y * y + z * z) - 3.2 + 1.6 * Billow(x, y, z)) * 1.5)) * max(0, min(1, (5.6 - sqrt(x * x + y * y + z * z)) * 2)) * max(0, min(1, (y + 0.5) / 2.5)) * Puff(x * 0.7, y * 0.7, z * 0.7) } }
    }
    media {
      mix subtract priority 1
      absorption rgb 6 scattering { 1, rgb 1.2 }
      density { function { Ball(x, y, z, 2.6, 0.5) } }
    }
  }
  translate F
}

#if (Smoke)
  box {
    F + <-12, -2, -11>, F + <13, 32, 12>
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        absorption rgb <1.5, 1.55, 1.65> scattering { 1, rgb 0.55 }
        density { function { Plume(x, y, z) } translate F }
      }
      media {
        mix multiply
        density {
          function { 0.5 + 0.5 * min(1, max(0, (sqrt(x * x + y * y + z * z) - 5.6) / 1.5)) * (2 * Billow(x * 0.5, y * 0.25, z * 0.5) - 1) }
          color_map { [0 rgb 0] [1 rgb 2] }
          translate F
        }
      }
    }
  }
  torus {
    7, 1.6 pigment { rgbt 1 }
    interior { media { mix add scattering { 1, rgb 0.25 } absorption 0.15 density { function { max(0, min(1, (Puff(x * 0.6, y * 0.6, z * 0.6) - 0.35) * 2.2)) } } } }
    scale <1, 0.6, 1> translate F * <1, 0, 1> + y * 0.6
  }
#end
