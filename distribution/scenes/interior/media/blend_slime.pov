// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A gelatinous cube built from layered media: gel, clear bubbles, gas pockets that replace it, ooze that adds, a band that multiplies, a fizz that subtracts, veins, cell walls, and eyes nested in the gas. +w480 +h360; Layers=N stops early.
#version 4.0;
#ifndef (Layers) #declare Layers = 9; #end
#declare R = seed(5);
#declare Ior = 1.25;

global_settings { assumed_gamma 1 max_trace_level 12 }
camera { location <-3.2, 3.2, -7.5> look_at <0, 1.6, 0> angle 46 }
background { rgb <0.01, 0.01, 0.015> }
light_source { <-6, 7, -5> rgb <1.5, 1.1, 0.7> }
light_source { <4, 4, -3> rgb <0.3, 0.45, 0.8> media_interaction off }
light_source { <-2, 5, -9> rgb 0.25 media_interaction off shadowless }
plane {
  y, 0
  texture {
    pigment { brick rgb <0.12, 0.11, 0.1> rgb <0.3, 0.27, 0.23> brick_size <1.2, 0.6, 0.8> mortar 0.05 rotate x * 90 }
    normal { granite 0.3 scale 0.3 }
    finish { diffuse 0.8 specular 0.1 }
  }
}
plane { -z, -4 pigment { rgb <0.09, 0.08, 0.07> } normal { granite 0.4 scale 0.5 } }
plane { -x, -5.5 pigment { rgb <0.09, 0.08, 0.07> } normal { granite 0.4 scale 0.5 } }

#declare Clear = texture { pigment { rgbt 1 } }
#macro Gel(Absorb, Scatter, Emit, Density)
  media { absorption Absorb scattering { 1, Scatter } emission Emit density { Density } }
#end
#macro Ramp(Lo, Hi) color_map { [0 rgb Lo] [1 rgb Hi] } #end

blob {
  threshold 0.55
  #for (I, -1, 1) #for (J, -1, 1) #for (K, -1, 1)
    sphere { <I, J * 0.8, K> * 1.05 + <rand(R), rand(R), rand(R)> * 0.25, 1.15, 1 }
  #end #end #end
  scale <1.15, 1, 1.05> translate y * 1.55
  texture {
    pigment { rgbf <0.8, 1, 0.75, 0.96> }
    normal { bumps 0.12 scale 0.4 }
    finish { specular 0.7 roughness 0.004 reflection { 0.03, 0.8 fresnel } conserve_energy }
  }
  interior { ior Ior Gel(rgb <0.35, 0.07, 0.3>, rgb <0.025, 0.06, 0.02>, rgb 0, density { bozo turbulence 0.3 Ramp(0.6, 1.1) scale 0.8 }) }
}

#if (Layers > 1)
  #for (I, 0, 22)
    sphere {
      <rand(R) * 2.8 - 1.4, 0.6 + rand(R) * 2.0, rand(R) * 2.4 - 1.2>, 0.05 + pow(rand(R), 3) * 0.18
      texture { pigment { rgbf <1, 1, 1, 0.98> } finish { specular 0.9 roughness 0.002 reflection { 0, 0.5 fresnel } } }
      interior { ior 1 }
    }
  #end
#end

#if (Layers > 2)
  #declare Gas = Gel(rgb 0.1, rgb <0.6, 0.15, 0.8>, rgb <0.7, 0.1, 0.9>, density { granite turbulence 0.6 Ramp(0, 1.5) scale 0.6 })
  #macro Pocket(Centre, Size)
    sphere { 0, 1 scale Size translate Centre texture { Clear } interior { ior Ior media_blend replace media { Gas } } }
  #end
  Pocket(<-0.75, 2.45, -1.25>, <0.5, 0.42, 0.38>)
  Pocket(<0.65, 2.5, -1.2>, <0.46, 0.4, 0.36>)
  Pocket(<1.0, 1.0, 0.6>, <0.6, 0.45, 0.55>)
#end

#if (Layers > 3)
  #for (I, 0, 1)
    #declare Eye = <-0.75 + I * 1.4, 2.47, -1.3>;
    sphere {
      Eye, 0.24 texture { Clear }
      interior { ior Ior Gel(rgb 0.2, rgb <0.8, 0.7, 0.1>, rgb <1.2, 0.8, 0.05>, density { onion turbulence 0.2 Ramp(0.2, 1) scale 0.06 translate Eye }) }
    }
    sphere {
      Eye - z * 0.12, 0.1 texture { Clear }
      interior { ior Ior media_blend replace media { absorption 30 } }
    }
  #end
#end

#if (Layers > 4)
  blob {
    threshold 0.5
    #for (I, 0, 6) sphere { <rand(R) * 1.8 - 0.9, 0.6 + rand(R) * 0.3, rand(R) * 1.6 - 0.8>, 0.45 + rand(R) * 0.2, 1 } #end
    texture { Clear }
    interior { ior Ior Gel(rgb <1.4, 0.5, 1.6>, rgb <0.1, 0.25, 0.04>, rgb 0, density { wrinkles Ramp(0, 2.2) scale 0.3 }) }
  }
#end

#if (Layers > 5)
  sphere {
    0, 1 scale <1.5, 0.22, 1.3> translate y * 1.85 texture { Clear }
    interior { ior Ior media_blend multiply media { density { marble turbulence 0.8 scale 0.7 color_map { [0 rgb 0.05] [1 rgb 0.6] } } } }
  }
#end

#if (Layers > 6)
  sphere {
    <-1.0, 1.0, 0.7>, 0.75 texture { Clear }
    interior {
      ior Ior media_blend subtract
      Gel(rgb <0.35, 0.07, 0.3>, rgb <0.025, 0.06, 0.02>, rgb 0, density { spherical turbulence 0.6 octaves 5 Ramp(0, 2.5) scale 0.75 translate <-1.0, 1.0, 0.7> })
    }
  }
#end

#if (Layers > 7)
  #for (I, 0, 6)
    #declare P = <rand(R) * 2 - 1, 0.45, rand(R) * 2 - 1> * <1.0, 1, 0.9>;
    sphere_sweep {
      b_spline 6
      #for (J, 0, 5)
        P, 0.035 - J * 0.004
        #declare P = P + <(rand(R) - 0.5) * 0.6, 0.4 + rand(R) * 0.15, (rand(R) - 0.5) * 0.6>;
      #end
      texture { Clear }
      interior { ior Ior media { absorption rgb <1, 10, 10> emission rgb <5, 0.4, 0.1> } }
    }
  #end
#end

#if (Layers > 8)
  #for (I, 0, 4)
    #declare C = <0.1 + rand(R) * 0.9, 0.9 + rand(R) * 1.1, -0.5 + rand(R) * 1.2>;
    #declare S = 0.28 + rand(R) * 0.18;
    difference {
      sphere { C, S } sphere { C, S - 0.025 }
      texture { Clear }
      interior { ior Ior media { scattering { 1, rgb <5, 6, 3> } absorption rgb <1, 0.3, 2> } }
    }
    sphere { C + <rand(R), rand(R), rand(R)> * S * 0.3, S * 0.3 texture { Clear } interior { ior Ior media { absorption rgb <1.5, 0.4, 3> } } }
  #end
#end
