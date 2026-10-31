// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Five dispersive glass prisms in one parallel photon beam, each splitting it into a spectrum. From the left: plain glass, then four whose index varies inside the glass through an interior refraction field added to the ior: a corkscrew that twists the index along the beam, 3D voronoi cells with one index each, a spherical gradient lens and laminated strata. The indices reach 2.0, past total internal reflection at every exit face, so some light leaves backwards: the blue loops in front of the lens are caustics of light that turned round inside it. +w1024 +h576 +a0.2
#version 3.8;
#declare H = 1.3;
#declare Slots = array[5] { -4.2, -2.1, 0, 2.1, 4.2 }
global_settings {
  assumed_gamma 1 max_trace_level 24
  photons { method 2 quality 1 }
}
camera { location <0, 11, -4.5> look_at <0, 0, 0.7> angle 58 right x * image_width / image_height }
background { rgb <0.004, 0.005, 0.012> }
sky_sphere {
  pigment { gradient y color_map { [0 rgb <0.01, 0.015, 0.04>] [0.45 rgb <0.005, 0.008, 0.02>] [0.58 rgb <0.55, 0.42, 0.30>] [0.66 rgb <0.01, 0.01, 0.03>] [1 rgb <0.01, 0.015, 0.05>] } }
}
light_source { <-5, 14, -9> rgb <1, 0.9, 0.78> * 0.10 shadowless photons { refraction off reflection off } }
plane { y, 0
  pigment {
    function { sqrt(x * x / 1.6 + (z - 0.8) * (z - 0.8)) / 9 }
    color_map { [0 rgb <0.30, 0.26, 0.22>] [0.3 rgb <0.08, 0.09, 0.13>] [0.65 rgb <0.01, 0.025, 0.07>] [1 rgb <0.003, 0.004, 0.012>] }
  }
  finish { ambient 0.015 diffuse 1 }
}
light_source { <0, H, -20> rgb 14 parallel point_at <0, H, 0> photon_only 1 photons { refraction on reflection off } }

#macro PrismIndexField(C)
  #switch (C)
    #case (1) pigment { function { 0.5 + 0.5 * sin(3 * atan2(y, x) + 12 * z) * min(1, 4 * sqrt(x * x + y * y)) } } #break
    #case (2) pigment { crackle solid scale 0.25 } #break
    #case (3) pigment { spherical scale 0.9 } #break
    #case (4) pigment { gradient x sine_wave turbulence 0.05 scale 0.3 } #break
  #end
#end
#macro Strength(C) #switch (C) #case (1) (0.5) #break #case (2) (0.6) #break #case (3) (0.5) #break #case (4) (0.5) #end #end

// C = 0 is plain glass
#macro GradientPrism(C, X)
  #if (C > 0) #local PrismIndex = function { PrismIndexField(C) } #end
  prism { linear_sweep linear_spline -0.9, 0.9, 4
    <-0.693, 0>, <0.346, 0.6>, <0.346, -0.6>, <-0.693, 0>
    pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 specular 0.3 roughness 0.005 reflection { 0, 1 fresnel on } conserve_energy }
    interior {
      ior 1.5 dispersion 1.04 dispersion_samples 31
      #if (C > 0) refraction { function { Strength(C) * PrismIndex(x, y, z).red } detail 0.05 } #end
    }
    rotate z * -90
    translate <X, H, 0>
    photons { target refraction on reflection off collect off }
  }
#end
#for (C, 0, 4) GradientPrism(C, Slots[C]) #end
