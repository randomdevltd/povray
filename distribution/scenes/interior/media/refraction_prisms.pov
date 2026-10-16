// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Four glass prisms whose index of refraction varies inside the glass: media refraction adds K times the medium's density to the interior's ior. From the left: octave turbulence, 3D voronoi cells with one index each, a spherical gradient lens and laminated strata. A parallel photon beam splits into a spectrum through each. +w1024 +h576 +a0.2.
// The indices reach 2.0, past total internal reflection at every exit face, so some light leaves backwards: the blue loops in front of the lens are caustics of light that turned round inside it. Declare=Case=1..4 renders one prism alone, Plain=1 removes the media, PhotonQuality raises the photon count, Bands sets the spectral samples.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (Dispersion) #declare Dispersion = 1.04; #end
#ifndef (Bands) #declare Bands = 31; #end
#ifndef (Power) #declare Power = 10; #end
#ifndef (Detail) #declare Detail = 0.5; #end
#ifndef (KScale) #declare KScale = 1; #end
#ifndef (Plain) #declare Plain = 0; #end
#declare H = 1.3;
#declare Slots = array[4] { -3.6, -1.2, 1.2, 3.6 }
global_settings {
  assumed_gamma 1 mm_per_unit 10 max_trace_level 24 refraction_detail Detail
  photons { method 2 quality PhotonQuality }
}
camera { location <0, 11, -4.5> look_at <0, 0, 0.7> angle 52 right x * image_width / image_height }
background { rgb <0.03, 0.035, 0.05> }
light_source { <-5, 14, -9> rgb <1, 0.96, 0.9> * 0.22 shadowless photons { refraction off reflection off } }
plane { y, 0
  pigment {
    function { sqrt(x * x + (z - 0.8) * (z - 0.8)) / 13 }
    color_map { [0 rgb <0.88, 0.87, 0.85>] [0.46 rgb <0.55, 0.55, 0.58>] [1 rgb <0.10, 0.11, 0.14>] }
  }
  finish { ambient 0.04 diffuse 1 }
}
light_source { <0, H, -20> rgb Power parallel point_at <0, H, 0> photon_only 1 photons { refraction on reflection off } }

#macro Density(C)
  #switch (C)
    #case (1) density { bozo turbulence 0.85 octaves 6 omega 0.55 lambda 2.2 scale 0.45 } #break
    #case (2) density { crackle solid scale 0.55 } #break
    #case (3) density { spherical scale 0.9 } #break
    #case (4) density { gradient x sine_wave turbulence 0.05 scale 0.3 } #break
  #end
#end
#macro Strength(C) #switch (C) #case (1) (0.55) #break #case (2) (0.6) #break #case (3) (0.5) #break #case (4) (0.5) #end #end

#macro MediumPrism(C, X)
  prism { linear_sweep linear_spline -0.9, 0.9, 4
    <-0.693, 0>, <0.346, 0.6>, <0.346, -0.6>, <-0.693, 0>
    pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 specular 0.3 roughness 0.005 reflection { 0, 1 fresnel on } conserve_energy }
    interior {
      ior 1.5 dispersion Dispersion dispersion_samples Bands
      #if (!Plain) media { method 4 refraction Strength(C) * KScale Density(C) } #end
    }
    rotate z * -90
    translate <(Case = 0 ? X : 0), H, 0>
    photons { target refraction on reflection off collect off }
  }
#end
#if (Case = 0)
  #for (C, 1, 4) #local P = Slots[C - 1]; MediumPrism(C, P) #end
#else
  MediumPrism(Case, 0)
#end
