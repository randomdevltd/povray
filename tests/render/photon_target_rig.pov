// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Photon target allocation: a low sun shines through a canopy of glass leaves (one easy target) onto a stone-edged pool whose wavy water and mirror floor throw twice-refracted caustics on the garden wall, and onto a clump of glass grass on the near bank (one hard target). Declares PhotonQuality, CanopyWeight and PatchWeight (the number after target), UseCanopy and UsePatch (isolate one target), SplitPatch (split_union for the grass, still one target), Parallel (1, the default, a parallel sun; 0 a sun SunDistance away), Power (the photon-only sun), PhotonOnly (1 drops the direct sun, sky and fill), Bands, Dispersion, PatchDispersion, Detail. Render with +w640 +h360 -a.
#version 3.8;
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (CanopyWeight) #declare CanopyWeight = 1; #end
#ifndef (PatchWeight) #declare PatchWeight = 1; #end
#ifndef (UseCanopy) #declare UseCanopy = 1; #end
#ifndef (UsePatch) #declare UsePatch = 1; #end
#ifndef (Dispersion) #declare Dispersion = 1.03; #end
#ifndef (PatchDispersion) #declare PatchDispersion = 1.3; #end
#ifndef (Bands) #declare Bands = 9; #end
#ifndef (Power) #declare Power = 3; #end
#ifndef (Detail) #declare Detail = 1; #end
#ifndef (Parallel) #declare Parallel = 1; #end
#ifndef (SunDistance) #declare SunDistance = 200; #end
#ifndef (SplitPatch) #declare SplitPatch = 0; #end
#ifndef (PhotonOnly) #declare PhotonOnly = 0; #end
#declare Sun = vnormalize(<-0.394, -0.616, 0.682>);
#declare SunColour = <0.8, 0.67, 0.5>;
global_settings {
  assumed_gamma 1 max_trace_level 16 refraction_detail Detail
  photons { method 2 quality PhotonQuality }
}
camera { location <-4.1, 1.55, -2.1> look_at <0.7, 1.85, 4.6> angle 84 right x * image_width / image_height }
#if (Parallel)
  light_source { -100 * Sun rgb Power * SunColour parallel point_at <0, 0, 0> photon_only 1 photons { refraction on reflection on } }
#else
  light_source { -SunDistance * Sun rgb Power * SunColour * SunDistance * SunDistance fade_power 2 fade_distance 0 photon_only 1 photons { refraction on reflection on } }
#end
#if (PhotonOnly)
  background { rgb <0.010, 0.014, 0.030> }
#else
  sky_sphere { pigment { gradient y color_map { [0 rgb <0.62, 0.72, 0.86>] [0.35 rgb <0.25, 0.42, 0.75>] [1 rgb <0.10, 0.22, 0.55>] } } }
  light_source { -100 * Sun rgb SunColour parallel point_at <0, 0, 0> photons { refraction off reflection off } }
  light_source { <-20, 40, -10> rgb <0.14, 0.18, 0.27> shadowless photons { refraction off reflection off } }
  light_source { <2, 3, 30> rgb <0.07, 0.06, 0.05> shadowless photons { refraction off reflection off } }
#end
#declare Stone = texture {
  pigment { granite color_map { [0 rgb <0.50, 0.44, 0.36>] [0.6 rgb <0.62, 0.56, 0.46>] [1 rgb <0.70, 0.66, 0.58>] } scale 0.6 }
  normal { granite 0.15 scale 0.3 }
  finish { ambient 0 diffuse 0.8 }
}
#declare Lawn = texture {
  pigment { granite color_map { [0 rgb <0.10, 0.22, 0.05>] [0.5 rgb <0.17, 0.33, 0.07>] [1 rgb <0.28, 0.42, 0.12>] } scale 0.25 }
  normal { bumps 0.4 scale 0.03 }
  finish { ambient 0 diffuse 0.75 }
}
#declare Clear = texture { pigment { rgbf 1 } finish { ambient 0 diffuse 0 specular 0.3 roughness 0.002 } }
#declare Water = texture { pigment { rgbf 1 } finish { ambient 0 diffuse 0 specular 0.8 roughness 0.001 reflection { 0, 1 fresnel on } conserve_energy } }
#declare Bark = texture {
  pigment { wood color_map { [0 rgb <0.20, 0.13, 0.08>] [1 rgb <0.30, 0.21, 0.13>] } turbulence 0.3 scale <0.08, 1, 0.08> }
  normal { wrinkles 0.8 scale <0.05, 0.4, 0.05> }
  finish { ambient 0 diffuse 0.8 }
}
plane { y, 0 texture { Lawn } }
box { <-4.3, -1, 0.75>, <4.3, 0.025, 6.6> texture { Stone scale 0.7 } }
difference {
  box { <-3.6, 0.005, 1.3>, <3.6, 0.62, 6.5> }
  box { <-3.3, 0.1, 1.6>, <3.3, 1, 6.2> }
  texture { Stone }
}
difference {
  box { <-3.72, 0.6, 1.18>, <3.72, 0.7, 6.62> }
  box { <-3.3, 0.5, 1.6>, <3.3, 1, 6.2> }
  texture { Stone scale 1.3 }
}
box { <-3.29, 0.105, 1.61>, <3.29, 0.12, 6.19>
  pigment { rgb <0.88, 0.96, 0.98> } finish { ambient 0 diffuse 0 reflection <0.88, 0.96, 0.98> }
  photons { reflection on refraction off collect off }
}
intersection {
  box { <-3.29, 0.13, 1.61>, <3.29, 0.6, 6.19> }
  plane { y, 0.5 texture { Water normal { waves 0.8 frequency 1.2 turbulence 0.35 scale 0.9 } } }
  texture { Clear }
  interior { ior 1.33 dispersion Dispersion dispersion_samples Bands }
  photons { refraction on reflection off collect off }
}
union {
  box { <-9, 0.005, 6.9>, <9, 3.6, 7.2> }
  box { <-9.1, 3.6, 6.82>, <9.1, 3.75, 7.28> }
  pigment { bozo color_map { [0 rgb <0.86, 0.84, 0.79>] [1 rgb <0.93, 0.92, 0.88>] } scale 0.4 }
  normal { granite 0.08 scale 0.5 }
  finish { ambient 0 diffuse 0.85 }
}
#declare Downsun = <-0.5, 0, 0.866>;
#declare TrunkTop = <3.2, 3.3, 1.4>;
#declare Shelter = <2.6, 4.3, -0.6>;
#declare NC = 9;
#declare Clusters = array[NC] {
  <0.0, 5.6, 1.6>, <2.0, 5.7, 1.6>, <4.0, 5.6, 1.6>,
  <-1.2, 5.6, 3.0>, <0.8, 5.8, 3.0>, <2.8, 5.6, 3.0>,
  <-2.4, 5.5, 4.4>, <-0.4, 5.6, 4.4>, <1.6, 5.5, 4.4>
}
#declare Boughs = array[NC + 5];
#for (I, 0, NC - 1) #declare Boughs[I] = Clusters[I]; #end
#declare Boughs[NC] = Shelter;
#declare Boughs[NC + 1] = Shelter + <1.9, 0, 1.0>;
#declare Boughs[NC + 2] = Shelter + <-1.8, 0, 0.9>;
#declare Boughs[NC + 3] = Shelter + <1.0, 0, -1.6>;
#declare Boughs[NC + 4] = Shelter + <-1.3, 0, -1.4>;
union {
  cone { <4.4, -0.2, 0.6>, 0.34, TrunkTop, 0.24 }
  sphere { TrunkTop, 0.24 }
  #for (I, 0, NC + 4)
    #local Mid = (TrunkTop + Boughs[I]) / 2 + <0, 0.3, 0>;
    cone { TrunkTop, 0.15, Mid, 0.09 }
    sphere { Mid, 0.09 }
    cone { Mid, 0.09, Boughs[I] - <0, 0.1, 0>, 0.03 }
  #end
  texture { Bark }
}
#if (UseCanopy)
  #declare R = seed(11);
  #declare Leaf = cylinder { -0.007 * y, 0.007 * y, 1 scale <0.32, 1, 0.16> }
  union {
    #for (I, 1, 600)
      #local Th = 2 * pi * rand(R);
      #local Rr = sqrt(rand(R));
      #local P = Shelter + <3.8 * Rr * cos(Th), 0.25 * (1 - Rr * Rr) + 0.12 * rand(R), 2.6 * Rr * sin(Th)>;
      object { Leaf rotate <-12 + 24 * rand(R), 360 * rand(R), -12 + 24 * rand(R)> translate P
        texture { pigment { rgbft <0.3 + 0.2 * rand(R), 0.8, 0.2, 0, 0.92> } finish { ambient 0 diffuse 0.6 specular 0.4 roughness 0.01 } } }
    #end
    #for (C, 0, NC - 1)
      #for (I, 1, 75)
        #local D = <rand(R) - 0.5, rand(R) - 0.5, rand(R) - 0.5>;
        #local P = Clusters[C] + 1.3 * pow(rand(R), 0.4) * vnormalize(D + <0, 0.001, 0>) * <1, 0.6, 1>;
        object { Leaf rotate <-35 + 70 * rand(R), 360 * rand(R), -35 + 70 * rand(R)> translate P
          texture { pigment { rgbf <0.25 + 0.15 * rand(R), 0.58, 0.15, 0.3> } finish { ambient 0 diffuse 0.6 specular 0.3 roughness 0.02 } } }
      #end
    #end
    interior { ior 1.5 }
    photons { target CanopyWeight refraction on reflection off collect off }
  }
#end
#if (UsePatch)
  #declare R = seed(7);
  union {
    #for (I, 0, 11)
      #local Az = 360 * (I + 0.6 * rand(R)) / 12;
      #local L = (0.8 + 0.7 * rand(R)) / 3;
      #local A = 8 + 22 * rand(R);
      #local Bend = 14 + 16 * rand(R);
      #local W = 0.032 + 0.008 * rand(R);
      #local P = <0.06 * rand(R), 0, 0>;
      union {
        #for (S, 0, 2)
          #local Q = P + L * <sin(radians(A)), cos(radians(A)), 0>;
          intersection { cone { 0, W * (1 - S / 3), y * L, W * (1 - (S + 1) / 3) + 0.001 } plane { -z, 0 } scale <0.08, 1, 2> rotate -z * A translate P }
          #local P = Q;
          #local A = A + Bend;
        #end
        rotate y * Az
      }
    #end
    #for (I, 0, 2)
      #local Top = <0.25 * (rand(R) - 0.5), 1.55 + 0.3 * rand(R), 0.25 * (rand(R) - 0.5)>;
      cone { 0, 0.007, Top, 0.003 }
      sphere { 0, 1 scale <0.022, 0.13, 0.022> translate Top + <0, 0.1, 0> }
    #end
    split_union SplitPatch
    texture { pigment { rgbft <0.3, 0.85, 0.15, 0, 0.8> } finish { ambient 0 diffuse 0.6 specular 0.5 roughness 0.008 } }
    interior { ior 1.52 dispersion PatchDispersion dispersion_samples Bands }
    translate <-2.6, 0, 0.55>
    photons { target PatchWeight refraction on reflection off collect off }
  }
#end
