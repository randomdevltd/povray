// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Photon aiming layouts, photon-only light on a white floor. Case 1: two glass spheres in line along the light. Case 2: a glass sphere the light reaches only through a mirror. Case 3: an opaque box hiding most of a glass sphere. Case 4: a sparse canopy of small glass discs. Case 5: 64 small glass spheres, each its own target. Declares Case, PhotonQuality, Parallel, and Method 1 with PhotonCount for classic photons. +w320 +h240 -a.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (Parallel) #declare Parallel = 0; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (PhotonCount) #declare PhotonCount = 200000; #end
global_settings { assumed_gamma 1 max_trace_level 12 photons { #if (Method = 1) count PhotonCount #else method 2 quality PhotonQuality #end } }
camera { location <0, 9, -9> look_at <0, 0, 0.5> angle 52 right x * image_width / image_height }
background { rgb 0 }
#declare Lamp = <-1, 9, -3>;
#if (Case = 2) #declare Lamp = <6, 6, -2>; #end
#if (Parallel)
  light_source { Lamp rgb 1 parallel point_at <0, 0, 0> photon_only on photons { refraction on reflection on } }
#else
  light_source { Lamp rgb 60 fade_power 2 fade_distance (Method = 1 ? sqrt(0.5) : 0) photon_only on photons { refraction on reflection on } }
#end
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#declare Glass = material { texture { pigment { rgbf 1 } finish { ambient 0 diffuse 0 specular 0.3 roughness 0.005 } } interior { ior 1.5 } }
#switch (Case)
  #case (1)
    sphere { Lamp + 0.45 * (<0, 1, 0> - Lamp), 0.8 material { Glass } photons { target refraction on reflection off collect off } }
    sphere { Lamp + 0.75 * (<0, 1, 0> - Lamp), 1 material { Glass } photons { target refraction on reflection off collect off } }
  #break
  #case (2)
    box { <-0.05, 0, -2>, <0.05, 4, 2> rotate y * 20 translate <-2.2, 0, 0.8>
      pigment { rgb 0 } finish { ambient 0 diffuse 0 reflection 1 } photons { target reflection on collect off } }
    box { <-0.8, 0, -1.6>, <1.0, 3.2, 1.6> translate <2.0, 0, 0.8> pigment { rgb 0.2 } finish { ambient 0 diffuse 0.5 } }
    sphere { <0.2, 1.2, 0.6>, 0.9 material { Glass } photons { target refraction on reflection off collect off } }
  #break
  #case (3)
    sphere { <0, 1.2, 0.5>, 1 material { Glass } photons { target refraction on reflection off collect off } }
    box { <-1.6, 2.4, -1.6>, <0.3, 2.5, 2.4> pigment { rgb 0.2 } finish { ambient 0 diffuse 0.5 } }
  #break
  #case (4)
    #declare R = seed(5);
    union {
      #for (I, 1, 60)
        #local P = <-4 + 8 * rand(R), 3 + 0.6 * rand(R), -3 + 7 * rand(R)>;
        cylinder { -0.01 * y, 0.01 * y, 0.22 rotate <30 * rand(R) - 15, 0, 30 * rand(R) - 15> translate P }
      #end
      material { Glass } photons { target refraction on reflection off collect off }
    }
  #break
  #case (5)
    #declare R = seed(9);
    #for (I, 0, 63)
      sphere { <-4 + 8 * rand(R), 0.15, -3 + 7 * rand(R)>, 0.15 material { Glass } photons { target refraction on reflection off collect off } }
    #end
  #break
#end
