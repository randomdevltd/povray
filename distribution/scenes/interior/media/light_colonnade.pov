// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A rotunda of twisted columns, arches, lattice screens, armillary bands and hanging chains, lit only by the glowing core at its heart. +w640 +h480; Declare=Samples=N sets the core light's samples, Refract=0 drops the lens of space around the core, Haze=0 clears the air, Chains=0 drops the chains.
#version 4.0;
#ifndef (Samples) #declare Samples = 32; #end
#ifndef (Refract) #declare Refract = 1; #end
#ifndef (Haze) #declare Haze = 1; #end
#ifndef (Chains) #declare Chains = 1; #end
#declare R = seed(1729);
#declare Core = <0, 3.6, 0>;
#declare Inner = 3.9;
#declare Outer = 8.4;
#declare WallR = 12.6;
#declare Lens = 7;
#declare Swirl = function { pattern { bozo turbulence 0.8 octaves 6 lambda 2.6 omega 0.55 scale 0.45 } }

global_settings { assumed_gamma 1 max_trace_level 6 refraction_angle 2 }
camera { location <0, 5.3, -11.9> look_at <0, 2.9, 0> angle 66 right x * image_width / image_height }
background { rgb <0.002, 0.003, 0.008> }

#declare Marble = texture {
  pigment { marble turbulence 0.9 octaves 5 lambda 2.4 color_map { [0 rgb <0.8, 0.76, 0.68>] [0.7 rgb <0.7, 0.66, 0.58>] [0.9 rgb <0.42, 0.38, 0.34>] [1 rgb <0.3, 0.27, 0.24>] } scale 1.4 }
  finish { diffuse 0.85 specular 0.15 roughness 0.02 }
}
#declare Bronze = texture { pigment { rgb <0.45, 0.28, 0.12> } finish { diffuse 0.55 specular 0.6 roughness 0.015 metallic reflection { 0.12 metallic } } }
#declare Iron = texture { pigment { rgb 0.07 } finish { diffuse 0.7 specular 0.35 roughness 0.03 } }
#macro At(Angle, Radius, Height) <Radius * sin(radians(Angle)), Height, Radius * cos(radians(Angle))> #end

cylinder {
  <0, -0.2, 0>, 0, WallR + 0.5
  pigment {
    function { mod(floor(atan2(x, z) * 16 / pi + 64) + floor(sqrt(x * x + z * z) / 0.9), 2) }
    color_map { [0.5 rgb <0.72, 0.66, 0.57>] [0.5 rgb <0.14, 0.11, 0.1>] }
  }
  finish { diffuse 0.85 specular 0.3 roughness 0.008 reflection 0.05 }
}
difference {
  cylinder { 0, y * 13, WallR + 0.6 }
  cylinder { -y, y * 14, WallR }
  #for (J, 0, 15)
    union { box { <-0.75, 0.9, -0.1>, <0.75, 3.6, 0.35> } cylinder { <0, 3.6, -0.1>, <0, 3.6, 0.35>, 0.75 } rotate y * (J + 0.5) * 22.5 translate At((J + 0.5) * 22.5, WallR, 0) }
    box { <-2.4, 6.5, -0.1>, <2.4, 6.9, 0.3> rotate y * (J + 0.5) * 22.5 translate At((J + 0.5) * 22.5, WallR, 0) }
  #end
  texture { Marble }
}

#declare Urn = lathe {
  cubic_spline 10,
  <0, -0.05>, <0, 0>, <0.2, 0.02>, <0.12, 0.15>, <0.34, 0.5>, <0.28, 0.9>, <0.15, 1.05>, <0.22, 1.2>, <0, 1.24>, <0, 1.3>
  texture { Bronze }
}
#for (J, 0, 15) object { Urn rotate y * J * 40 translate At((J + 0.5) * 22.5, WallR + 0.02, 0.9) } #end

#declare Column = union {
  cylinder { y * 0.55, y * 8.3, 0.4 texture { Marble normal { radial 0.5 frequency 24 sine_wave } } }
  box { <-0.6, 0, -0.6>, <0.6, 0.4, 0.6> }
  torus { 0.45, 0.09 translate y * 0.5 }
  torus { 0.42, 0.08 translate y * 8.3 }
  box { <-0.6, 8.4, -0.6>, <0.6, 8.8, 0.6> }
  texture { Marble }
}
#for (J, 0, 15) object { Column rotate y * J * 22.5 translate At((J + 0.5) * 22.5, Outer, 0) } #end
difference {
  cylinder { y * 8.8, y * 9.9, Outer + 0.8 }
  cylinder { y * 8.7, y * 10, Outer - 0.75 }
  #for (J, 0, 63) box { <-0.1, 9.25, -2>, <0.1, 9.55, 2> rotate y * J * 5.625 translate At(J * 5.625, Outer + 0.8, 0) } #end
  texture { Marble }
}

#declare Trellis = union {
  intersection {
    union {
      #for (K, -14, 14)
        box { <-0.035, -4, -0.03>, <0.035, 4, 0.03> rotate z * 38 translate x * K * 0.3 }
        box { <-0.035, -4, -0.03>, <0.035, 4, 0.03> rotate z * -38 translate x * K * 0.3 }
      #end
    }
    box { <-1, -2.3, -0.1>, <1, 2.3, 0.1> }
  }
  difference { box { <-1.08, -2.4, -0.06>, <1.08, 2.4, 0.06> } box { <-1, -2.3, -1>, <1, 2.3, 1> } }
  translate y * 2.4
  texture { Bronze }
}
#for (J, 0, 15, 2) #if (J != 8) object { Trellis rotate y * J * 22.5 translate At(J * 22.5, Outer * cos(radians(11.25)), 0) } #end #end

#declare Twist = union {
  #for (S, 0, 2)
    sphere_sweep {
      cubic_spline 26,
      #for (I, -1, 24)
        #local A = I / 23 * 5 * pi + S * 2 * pi / 3;
        <0.16 * cos(A), 0.45 + I / 23 * 5.1, 0.16 * sin(A)>, 0.11
      #end
    }
  #end
  cylinder { y * 0.4, y * 5.6, 0.13 }
  box { <-0.42, 0, -0.42>, <0.42, 0.45, 0.42> }
  box { <-0.4, 5.55, -0.4>, <0.4, 6, 0.4> }
  torus { 0.3, 0.07 translate y * 0.5 }
  torus { 0.3, 0.07 translate y * 5.5 }
  texture { Marble }
}
#declare Half = 0.5 * 2 * Inner * sin(radians(22.5));
#declare Mid = Inner * cos(radians(22.5));
union {
  #for (I, 0, 7)
    object { Twist rotate y * I * 45 translate At(I * 45 + 22.5, Inner, 0) }
    torus { Half, 0.15 rotate x * 90 clipped_by { plane { -y, 0 } } rotate y * (I * 45 + 45) translate At(I * 45 + 45, Mid, 6) }
    torus { Inner, 0.09 rotate z * 90 clipped_by { plane { -y, 0 } } rotate y * I * 22.5 translate y * 6 }
  #end
  torus { Inner, 0.16 translate y * 6.05 }
  torus { Inner * 0.5, 0.12 translate y * 6 + y * Inner * sqrt(0.75) }
  texture { Bronze }
}

#if (Refract)
  intersection {
    sphere { 0, Lens }
    plane { -y, Core.y - 0.05 }
    pigment { rgbt 1 }
    interior {
      media {
        method 4 resolution 0.14 refraction 0.25
        density {
          function {
            1.576 / max(sqrt(x * x + y * y + z * z), 1.7) * pow(max(0, 1 - (x * x + y * y + z * z) / (Lens * Lens)), 2) * (0.94 + 0.06 * Swirl(x, y, z))
          }
        }
      }
    }
    translate Core
  }
#end
union {
  #for (K, 0, 4)
    difference {
      cylinder { -y * 0.07, y * 0.07, 2.25 }
      cylinder { -y, y, 2.12 }
      #for (H, 0, 23) cylinder { <0, 0, 2.1>, <0, 0, 2.3>, 0.045 rotate y * H * 15 } #end
      rotate <K * 36 + 18, K * 71, 0>
    }
  #end
  cylinder { -y * 2.4, y * 2.4, 0.05 }
  sphere { -y * 2.4, 0.12 } sphere { y * 2.4, 0.12 }
  translate Core
  texture { Bronze }
}
cylinder { Core + y * 2.4, y * (6 + Inner), 0.04 texture { Iron } }

#if (Chains)
  #declare Link = torus { 0.07, 0.02 scale <1, 1, 1.5> rotate x * 90 }
  union {
    #for (I, 0, 7)
      #if (I != 3)
      #local Top = 7.35;
      #local Bottom = 1.25 + rand(R) * 1.4;
      #local N = int((Top - Bottom) / 0.17);
      #for (L, 0, N)
        object { Link rotate y * (90 * mod(L, 2) + 7 * rand(R)) translate At(I * 45 + 45, Mid, Top - L * 0.17) }
      #end
      union {
        cone { 0, 0.03, -y * 0.5, 0.2 }
        cylinder { -y * 0.5, -y * 0.55, 0.22 }
        #for (B, 0, 5) cylinder { -y * 0.55, -y * 0.95 + x * 0.08, 0.02 rotate y * B * 60 } #end
        sphere { -y * 1, 0.09 }
        translate At(I * 45 + 45, Mid, Top - N * 0.17 - 0.1)
      }
      #end
    #end
    texture { Iron }
  }
#end

union {
  #for (K, 0, 71)
    #local A = K * 5;
    cylinder { 0, y * (0.35 + 0.25 * mod(K, 3)), 0.05 translate At(A + 2.5, 5.1, 0) }
  #end
  #for (K, 0, 39)
    #local A = 195 + rand(R) * 330;
    #local D = 5.8 + rand(R) * 1.8;
    #local S = 0.18 + rand(R) * 0.22;
    #switch (mod(K, 4))
      #case (0) box { -S, S rotate <rand(R), rand(R), rand(R)> * 90 translate At(A, D, S * 1.2) } #break
      #case (1) union { cylinder { 0, y * S * 2, S * 0.6 } sphere { y * (S * 2 + S * 0.7), S * 0.7 } translate At(A, D, 0) } #break
      #case (2) cone { 0, S * 0.7, y * S * 6, 0 translate At(A, D, 0) } #break
      #case (3) torus { S, S * 0.3 rotate x * 90 rotate y * rand(R) * 180 translate At(A, D, S * 1.3) } #break
    #end
  #end
  texture { Marble }
}

sphere {
  0, 1.7 pigment { rgbt 1 }
  interior {
    media {
      mix add
      emission rgb <1, 0.6, 0.28> * 140
      density {
        function {
          min(1, 0.75 * exp(-(x * x + y * y + z * z) / 0.1)
                 + 0.25 * pow(max(0, Swirl(x, y, z) - 0.5) * 2, 2) * max(0, 1 - sqrt(x * x + y * y + z * z) / 1.7))
        }
      }
      light_source { samples Samples }
    }
  }
  translate Core
}

#if (Haze)
  cylinder {
    -y * 0.1, y * 13, WallR - 0.01 pigment { rgbt 1 }
    interior { media { mix add scattering { 5, rgb 0.012 eccentricity 0.4 } } }
  }
#end
