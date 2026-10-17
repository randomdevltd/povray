// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Photon target allocation: one photon-only sun, a wide wavy glass canopy over a mirror-floored pool that sends light out twice refracted onto the white wall, and a sparse clump of glass blades on the near bank that is hard to hit. Declares PhotonQuality, CanopyWeight and PatchWeight (the number after target), UseCanopy and UsePatch (isolate one target), Parallel (parallel sun instead of a distant point sun), Bands. +w640 +h360 -a.
#version 3.8;
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (CanopyWeight) #declare CanopyWeight = 1; #end
#ifndef (PatchWeight) #declare PatchWeight = 1; #end
#ifndef (UseCanopy) #declare UseCanopy = 1; #end
#ifndef (UsePatch) #declare UsePatch = 1; #end
#ifndef (Dispersion) #declare Dispersion = 1.03; #end
#ifndef (PatchDispersion) #declare PatchDispersion = 1.12; #end
#ifndef (Bands) #declare Bands = 9; #end
#ifndef (Power) #declare Power = 1.5; #end
#ifndef (Detail) #declare Detail = 1; #end
#ifndef (Parallel) #declare Parallel = 0; #end
#ifndef (SunDistance) #declare SunDistance = 200; #end
#declare Sun = vnormalize(<-0.25, -1.2, 1>);
global_settings {
  assumed_gamma 1 max_trace_level 16 refraction_detail Detail
  photons { method 2 quality PhotonQuality }
}
camera { location <0.8, 2.6, -5.0> look_at <0.3, 0.9, 6> angle 82 right x * image_width / image_height }
background { rgb <0.010, 0.014, 0.030> }
#if (Parallel)
  light_source { -100 * Sun rgb Power parallel point_at <0, 0, 0> photon_only 1 photons { refraction on reflection on } }
#else
  light_source { -SunDistance * Sun rgb Power * SunDistance * SunDistance photon_only 1 photons { refraction on reflection on } }
#end
light_source { <-4, 9, -8> rgb 0.05 photons { refraction off reflection off } }
#declare Stone = texture { pigment { granite color_map { [0 rgb 0.30] [1 rgb 0.42] } scale 2 } finish { ambient 0 diffuse 0.9 } }
#declare Clear = texture { pigment { rgbf 1 } finish { ambient 0 diffuse 0 specular 0.3 roughness 0.002 } }
plane { y, 0 texture { Stone } }
difference {
  box { <-3.6, 0.005, 1.3>, <3.6, 0.62, 6.5> }
  box { <-3.3, 0.1, 1.6>, <3.3, 1, 6.2> }
  texture { Stone }
}
box { <-3.29, 0.105, 1.61>, <3.29, 0.12, 6.19>
  pigment { rgb 1 } finish { ambient 0 diffuse 0 reflection 1 }
  photons { reflection on refraction off collect off }
}
intersection {
  box { <-3.29, 0.13, 1.61>, <3.29, 0.6, 6.19> }
  plane { y, 0.5 texture { Clear normal { waves 0.3 frequency 1.2 turbulence 0.35 scale 0.55 } } }
  texture { Clear }
  interior { ior 1.33 dispersion Dispersion dispersion_samples Bands }
  photons { refraction on reflection off collect off }
}
box { <-7, 0.005, 6.9>, <7, 7.5, 7.2> pigment { rgb 0.7 } finish { ambient 0 diffuse 1 } }
#if (UseCanopy)
  intersection {
    box { <-6, 3.7, -1>, <6, 4.2, 4.5> }
    plane { y, 4.0 texture { Clear normal { bumps 0.3 turbulence 0.2 scale 0.9 } } }
    texture { Clear }
    interior { ior 1.5 }
    photons { target CanopyWeight refraction on reflection off collect off }
  }
#end
#if (UsePatch)
  #declare R = seed(7);
  union {
    #for (I, 0, 3)
      #local A = 360 * (I + 0.5 * rand(R)) / 4;
      #local L = 0.22 + 0.08 * rand(R);
      sphere { 0, 1 scale <0.024 + 0.006 * rand(R), L, 0.0011> rotate <-25 + 50 * rand(R), -35 + 70 * rand(R), -20 + 40 * rand(R)>
        translate <0.55 * sin(radians(A)), L + 0.03, 0.55 * cos(radians(A))> }
    #end
    // one target: a split union would make each blade its own target
    split_union off
    texture { Clear }
    interior { ior 1.52 dispersion PatchDispersion dispersion_samples Bands }
    scale 1.8 translate <0.9, 0, -0.35>
    photons { target PatchWeight refraction on reflection off collect off }
  }
#end
