// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// An underground passage lit only by fluorescent tubes and neon strips, one tube dying and one dead; chairs, a trolley, a bicycle, cones, crates and a railing stretch their shadows along the tubes. +w640 +h480; Declare=Samples=N sets each tube's samples, Haze=0 clears the air.
#version 4.0;
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Haze) #declare Haze = 1; #end
#declare R = seed(314);
#declare Top = 3.6;
#declare Half = 4.4;
#declare Far = 38;

global_settings { assumed_gamma 1 max_trace_level 6 }
camera { location <0.6, 1.45, -1.2> look_at <-0.4, 1.2, 14> angle 72 right x * image_width / image_height }
background { rgb 0 }

#declare Concrete = texture {
  pigment { granite turbulence 0.4 color_map { [0 rgb 0.5] [0.5 rgb 0.44] [1 rgb 0.36] } scale 0.6 }
  normal { granite 0.15 scale 0.08 }
  finish { diffuse 0.85 }
}
#declare Steel = texture { pigment { rgb 0.45 } finish { diffuse 0.5 specular 0.6 roughness 0.01 metallic reflection { 0.15 metallic } } }
#declare Paint = texture { pigment { rgb <0.75, 0.6, 0.08> } finish { diffuse 0.8 specular 0.2 } }

box {
  <-Half, -0.1, -8>, <Half, 0, Far>
  texture {
    pigment_pattern { bozo turbulence 0.6 octaves 4 scale <1.6, 1, 2.4> }
    texture_map {
      [0.6 Concrete]
      [0.6 pigment { rgb 0.18 } finish { diffuse 0.4 specular 0.9 roughness 0.002 reflection 0.55 }]
    }
  }
}
box { <-0.06, 0, -8>, <0.06, 0.003, Far> texture { Paint } }
#for (Z, -6, Far, 2.5) box { <-Half, 0, Z - 0.05>, <-2.9, 0.003, Z + 0.05> texture { Paint } } #end
box { <-Half, Top, -8>, <Half, Top + 0.2, Far> texture { Concrete } }
box {
  <-Half - 0.2, 0, -8>, <-Half, Top, Far>
  texture { pigment { brick rgb 0.3, rgb <0.55, 0.53, 0.5> brick_size <0.6, 0.3, 0.6> mortar 0.015 } normal { granite 0.1 scale 0.1 } finish { diffuse 0.85 } }
}
box {
  <Half, 0, -8>, <Half + 0.2, Top, Far>
  texture { pigment { brick rgb 0.12, rgb <0.5, 0.64, 0.56> brick_size <0.3, 0.15, 0.3> mortar 0.012 } finish { diffuse 0.75 specular 0.5 roughness 0.004 reflection 0.06 } }
}
box { <-Half, 0, Far>, <Half, Top, Far + 0.2> texture { Concrete } }
box { <-Half, 0, -8.2>, <Half, Top, -8> texture { Concrete } }

#for (Z, 0, Far - 1, 6)
  box { <-Half, Top - 0.35, Z - 0.2>, <Half, Top, Z + 0.2> texture { Concrete } }
  box { <-2.85, 0, Z - 0.25>, <-2.35, Top, Z + 0.25> texture { Concrete } }
  box { <2.35, 0, Z - 0.25>, <2.85, Top, Z + 0.25> texture { Concrete } }
  box { <-2.86, 0, Z - 0.26>, <-2.34, 0.9, Z + 0.26> texture { Paint } }
  box { <2.34, 0, Z - 0.26>, <2.86, 0.9, Z + 0.26> texture { Paint } }
#end
union {
  cylinder { <-1.5, Top - 0.18, -8>, <-1.5, Top - 0.18, Far>, 0.09 }
  cylinder { <-1.25, Top - 0.12, -8>, <-1.25, Top - 0.12, Far>, 0.05 }
  cylinder { <3.6, Top - 0.25, -8>, <3.6, Top - 0.25, Far>, 0.16 }
  #for (Z, -6, Far, 3) torus { 0.17, 0.02 rotate z * 90 translate <3.6, Top - 0.25, Z> } #end
  texture { Steel }
}

#macro Tube(P, Length, Colour, Strength)
  union {
    box { <-0.04, 0.06, -Length / 2 - 0.1>, <0.04, 0.12, Length / 2 + 0.1> }
    box { <-0.04, -0.04, -Length / 2 - 0.1>, <0.04, 0.12, -Length / 2> }
    box { <-0.04, -0.04, Length / 2>, <0.04, 0.12, Length / 2 + 0.1> }
    texture { pigment { rgb 0.7 } finish { diffuse 0.7 } }
    translate P
  }
  #if (Strength > 0)
    cylinder {
      <0, 0, -Length / 2>, <0, 0, Length / 2>, 0.028
      pigment { rgbt 1 }
      interior { media { mix add method 4 resolution 0.009 emission Colour * 1200 * Strength light_source { samples Samples } } }
      translate P
    }
  #end
#end
#for (K, 0, 6)
  #local Strength = (K = 3 ? 0.12 : (K = 5 ? 0 : 1));
  Tube(<0, Top - 0.5, 3 + K * 6>, 2.4, rgb <1, 0.96, 0.88>, Strength)
#end

cylinder {
  <-Half + 0.08, 2.25, -1>, <-Half + 0.08, 2.25, 28>, 0.024
  pigment { rgbt 1 }
  interior { media { mix add method 4 resolution 0.012 emission rgb <1, 0.02, 0.3> * 160 light_source { samples Samples } } }
}
union {
  #for (Z, -1, 28, 1.5) cylinder { <-Half, 2.25, Z>, <-Half + 0.1, 2.25, Z>, 0.008 } #end
  texture { Steel }
}
sphere_sweep {
  linear_spline 12,
  #for (I, 0, 11) <Half - 0.08, 1.3 + 0.55 * mod(I, 2) + 0.1 * sin(I), 7.2 + I * 0.55>, 0.024 #end
  pigment { rgbt 1 }
  interior { media { mix add method 4 resolution 0.012 emission rgb <0.04, 0.75, 1> * 160 light_source { samples Samples } } }
}

#macro Chair(P, A)
  union {
    box { <-0.24, 0.44, -0.22>, <0.24, 0.48, 0.22> }
    box { <-0.24, 0.48, 0.18>, <0.24, 0.92, 0.22> rotate x * -8 translate <0, 0.04, 0> }
    #for (I, -1, 1, 2) #for (J, -1, 1, 2) cylinder { <I * 0.21, 0, J * 0.19>, <I * 0.21, 0.44, J * 0.19>, 0.016 } #end #end
    cylinder { <-0.21, 0.08, -0.19>, <-0.21, 0.08, 0.19>, 0.01 }
    cylinder { <0.21, 0.08, -0.19>, <0.21, 0.08, 0.19>, 0.01 }
    texture { pigment { rgb <0.1, 0.22, 0.45> } finish { diffuse 0.7 specular 0.4 roughness 0.01 } }
    rotate y * A translate P
  }
#end
#for (K, 0, 6) Chair(<1.7, 0, 2.6 + K * 0.6>, 180 + (K = 4 ? 25 : 0)) #end
#for (K, 0, 3) Chair(<-1.9, 0, 13 + K * 0.6>, 0) #end

union {
  #for (I, 0, 9) cylinder { <-0.3 + I * 0.0667, 0.38, 0>, <-0.3 + I * 0.0667, 0.95, 0>, 0.005 } #end
  #for (I, 0, 13) cylinder { <-0.3 + I * 0.0462, 0.38, 0.9>, <-0.3 + I * 0.0462, 0.95, 0.9>, 0.005 } #end
  #for (I, 0, 15) cylinder { <-0.3, 0.38, I * 0.06>, <-0.3, 0.95, I * 0.06>, 0.005 } cylinder { <0.3, 0.38, I * 0.06>, <0.3, 0.95, I * 0.06>, 0.005 } #end
  #for (J, 0, 6)
    #local Yj = 0.38 + J * 0.095;
    cylinder { <-0.3, Yj, 0>, <0.3, Yj, 0>, 0.005 } cylinder { <-0.3, Yj, 0.9>, <0.3, Yj, 0.9>, 0.005 }
    cylinder { <-0.3, Yj, 0>, <-0.3, Yj, 0.9>, 0.005 } cylinder { <0.3, Yj, 0>, <0.3, Yj, 0.9>, 0.005 }
  #end
  #for (I, 0, 10) cylinder { <-0.3, 0.38, I * 0.09>, <0.3, 0.38, I * 0.09>, 0.005 } #end
  cylinder { <-0.3, 0.38, 0>, <-0.25, 0.1, 0>, 0.015 } cylinder { <0.3, 0.38, 0>, <0.25, 0.1, 0>, 0.015 }
  cylinder { <-0.3, 0.38, 0.9>, <-0.25, 0.1, 0.9>, 0.015 } cylinder { <0.3, 0.38, 0.9>, <0.25, 0.1, 0.9>, 0.015 }
  cylinder { <-0.3, 0.95, 0.95>, <0.3, 0.95, 0.95>, 0.016 }
  #for (I, -1, 1, 2) #for (J, 0, 1) torus { 0.05, 0.015 rotate z * 90 translate <I * 0.25, 0.05, J * 0.9> } #end #end
  texture { Steel }
  rotate y * 28 translate <-0.9, 0, 6.4>
}

#macro Wheel(C)
  union {
    torus { 0.33, 0.018 rotate x * 90 }
    #for (S, 0, 31) cylinder { 0, <0.33 * cos(S * pi / 16), 0.33 * sin(S * pi / 16), 0.02 * (mod(S, 2) - 0.5)>, 0.0025 } #end
    translate C
  }
#end
union {
  Wheel(<-0.52, 0.35, 0>)
  Wheel(<0.52, 0.35, 0>)
  cylinder { <-0.52, 0.35, 0>, <-0.05, 0.38, 0>, 0.015 }
  cylinder { <-0.05, 0.38, 0>, <0.42, 0.85, 0>, 0.017 }
  cylinder { <-0.05, 0.38, 0>, <-0.15, 0.85, 0>, 0.017 }
  cylinder { <-0.15, 0.85, 0>, <0.42, 0.85, 0>, 0.017 }
  cylinder { <-0.52, 0.35, 0>, <-0.15, 0.85, 0>, 0.012 }
  cylinder { <0.52, 0.35, 0>, <0.42, 0.95, 0>, 0.017 }
  cylinder { <0.42, 0.95, -0.22>, <0.42, 0.95, 0.22>, 0.012 }
  box { <-0.26, 0.86, -0.05>, <-0.06, 0.9, 0.05> }
  texture { pigment { rgb <0.6, 0.08, 0.05> } finish { diffuse 0.7 specular 0.5 roughness 0.01 } }
  rotate x * -14 rotate y * 90 translate <2.1, 0, 11.4>
}

union {
  #for (K, 0, 5)
    #local P = <-0.4 + rand(R) * 2.2, 0, 8 + K * 2.6 + rand(R)>;
    union {
      box { <-0.2, 0, -0.2>, <0.2, 0.03, 0.2> }
      cone { 0, 0.15, y * 0.7, 0.02 }
      texture { pigment { gradient y color_map { [0 rgb <1, 0.25, 0.02>] [0.55 rgb <1, 0.25, 0.02>] [0.55 rgb 0.9] [0.75 rgb 0.9] [0.75 rgb <1, 0.25, 0.02>] } scale 0.7 } finish { diffuse 0.8 } }
      #if (K = 2) rotate z * 90 translate y * 0.15 #end
      rotate y * rand(R) * 90 translate P
    }
  #end
}

union {
  #for (I, 0, 2)
    #for (J, 0, 2 - I)
      union {
        #for (S, 0, 4) box { <-0.3, S * 0.11, -0.3>, <0.3, S * 0.11 + 0.08, 0.3> } #end
        #for (C, 0, 3) box { <-0.3, 0, -0.3>, <-0.24, 0.52, -0.24> rotate y * C * 90 } #end
        rotate y * (rand(R) * 16 - 8) translate <-3.7 + J * 0.64 + I * 0.32, I * 0.53, 4.2 + rand(R) * 0.1>
      }
    #end
  #end
  texture { pigment { rgb <0.5, 0.36, 0.2> } normal { wood 0.3 scale 0.02 rotate y * 90 } finish { diffuse 0.8 } }
}

union {
  #for (K, -60, 60)
    cylinder { <0, -1.4, -1.4>, <0, 1.4, 1.4>, 0.006 translate z * K * 0.09 }
    cylinder { <0, -1.4, 1.4>, <0, 1.4, -1.4>, 0.006 translate z * K * 0.09 }
  #end
  clipped_by { box { <-0.1, -1.15, -2.7>, <0.1, 1.15, 2.7> } }
  translate <-2.6, 1.2, 9>
  texture { Steel }
}
union {
  cylinder { <-2.6, 2.35, 6.3>, <-2.6, 2.35, 11.7>, 0.025 }
  cylinder { <-2.6, 0.05, 6.3>, <-2.6, 0.05, 11.7>, 0.025 }
  #for (Z, 6.3, 11.8, 1.8) cylinder { <-2.6, 0, Z>, <-2.6, 2.4, Z>, 0.03 } #end
  texture { Steel }
}

union {
  #for (Z, 15, 27, 0.18) cylinder { <-3.7, 0, Z>, <-3.7, 1.0, Z>, 0.012 } #end
  cylinder { <-3.7, 1.0, 15>, <-3.7, 1.0, 27>, 0.03 }
  cylinder { <-3.7, 0.5, 15>, <-3.7, 0.5, 27>, 0.015 }
  texture { Steel }
}

union {
  #for (K, 0, 4)
    #local A = <2.6 - K * 0.15, Top - 0.6, K * 6 + 0.3>;
    #local B = <-2.6 + K * 0.1, Top - 0.5, K * 6 + 0.4>;
    sphere_sweep { cubic_spline 5, A + y, 0.012, A, 0.012, (A + B) / 2 - y * (0.9 + rand(R) * 0.5), 0.012, B, 0.012, B + y, 0.012 }
  #end
  texture { pigment { rgb 0.05 } }
}

#if (Haze)
  box {
    <-Half + 0.01, 0.01, -7.99>, <Half - 0.01, Top - 0.01, Far - 0.01>
    pigment { rgbt 1 }
    interior { media { mix add scattering { 5, rgb 0.0012 eccentricity 0.3 } } }
  }
#end
