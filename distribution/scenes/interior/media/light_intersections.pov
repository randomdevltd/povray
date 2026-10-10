// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Emitting media meeting things: red, green and blue lights overlapping, threaded by a bronze knot, holding a glass ball and crossed by smoke; a glow sealed in a cracked stone; a pool sunk in the floor, pierced by rods. +w640 +h480; Declare=Samples=N sets each light's samples, Smoke=0 drops the smoke.
#version 4.0;
#ifndef (Samples) #declare Samples = 24; #end
#ifndef (Smoke) #declare Smoke = 1; #end
#declare R = seed(42);
#declare Mid = <0, 2.9, 0.3>;
#declare Geode = <3.5, 0.92, 0.9>;
#declare Pool = <-3.3, 0, -0.6>;

global_settings { assumed_gamma 1 max_trace_level 10 }
camera { location <1.2, 2.7, -8.2> look_at <0, 1.75, 0.6> angle 62 right x * image_width / image_height }
background { rgb 0 }

#declare White = texture { pigment { rgb 0.8 } finish { diffuse 0.9 specular 0.1 roughness 0.05 } }
plane { y, 0 texture { pigment { rgb 0.72 } finish { diffuse 0.85 specular 0.25 roughness 0.01 reflection 0.04 } } }
plane { -z, -5.5 texture { pigment { rgb 0.75 } finish { diffuse 0.85 } } }
plane { x, -7 texture { pigment { rgb 0.7 } finish { diffuse 0.85 } } }

#macro Lamp(Centre, Colour)
  sphere {
    0, 1 pigment { rgbt 1 }
    interior {
      media {
        mix add
        emission Colour * 14
        density { spherical turbulence 0.12 octaves 3 color_map { [0 rgb 0] [1 rgb 1] } }
        light_source { samples Samples }
      }
    }
    translate Centre
  }
#end
Lamp(Mid + <-0.55, -0.32, 0>, rgb <1, 0.02, 0.01>)
Lamp(Mid + <0.55, -0.32, 0>, rgb <0.01, 1, 0.03>)
Lamp(Mid + <0, 0.63, 0>, rgb <0.02, 0.06, 1>)

sphere_sweep {
  cubic_spline 75,
  #for (I, -1, 73)
    #local T = I / 72 * 2 * pi;
    <(2 + cos(3 * T)) * cos(2 * T), (2 + cos(3 * T)) * sin(2 * T), 1.1 * sin(3 * T)> * 0.42 + Mid, 0.065
  #end
  texture { pigment { rgb <0.35, 0.22, 0.1> } finish { diffuse 0.6 specular 0.6 roughness 0.01 metallic reflection { 0.2 metallic } } }
}

#if (Smoke)
  cone {
    <1.8, 0, 0.9>, 0.35, <0.6, 7, 0.6>, 1.4
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        scattering { 1, rgb 0.3 } absorption rgb 3
        density {
          cylindrical turbulence <0.6, 0.1, 0.6> octaves 5 lambda 2.5 scale <0.5, 1, 0.5>
          color_map { [0 rgb 0] [0.45 rgb 0] [0.8 rgb 0.7] [1 rgb 1] }
          warp { turbulence <0.4, 0.2, 0.4> octaves 4 lambda 2.2 }
          rotate z * 10 translate <1.8, 0, 0.9>
        }
      }
    }
  }
#end

sphere {
  Mid + <0, 0, -0.15>, 0.3
  pigment { rgbf <0.97, 0.99, 1, 1> }
  finish { diffuse 0 specular 0.8 roughness 0.002 reflection { 0 1 fresnel } conserve_energy }
  interior { ior 1.5 media { mix add } }
}

difference {
  sphere { 0, 1 }
  sphere { 0, 0.9 }
  sphere { <-0.5, 0.45, -0.75>, 0.6 }
  #for (K, 0, 9)
    box { <-1.2, -0.02 - rand(R) * 0.03, -1.2>, <1.2, 0.02 + rand(R) * 0.03, 1.2> rotate <rand(R) * 180, rand(R) * 180, rand(R) * 180> translate <rand(R) - 0.5, rand(R) - 0.5, rand(R) - 0.5> * 0.4 }
  #end
  texture { pigment { granite color_map { [0 rgb 0.12] [0.5 rgb 0.18] [1 rgb 0.06] } scale 0.4 } normal { granite 0.4 scale 0.2 } finish { diffuse 0.8 } }
  translate Geode
}
sphere {
  0, 0.89 pigment { rgbt 1 }
  interior {
    media {
      mix add
      emission rgb <1, 0.72, 0.38> * 60
      density { spherical turbulence 0.3 octaves 4 color_map { [0 rgb 0.1] [1 rgb 1] } }
      light_source { samples Samples }
    }
  }
  translate Geode
}

sphere {
  0, 1 pigment { rgbt 1 }
  interior {
    media {
      mix add
      emission rgb <1, 0.38, 0.06> * 12
      density { spherical turbulence 0.35 octaves 5 lambda 2.4 color_map { [0 rgb 0] [0.3 rgb 0.2] [1 rgb 1] } }
      light_source { samples Samples }
    }
  }
  scale <1.6, 0.7, 1.2> translate Pool
}
union {
  #for (K, 0, 8)
    #local A = K * 40 + rand(R) * 20;
    #local D = 0.3 + rand(R) * 1.1;
    cylinder { 0, <rand(R) - 0.5, 4, rand(R) - 0.5> * (0.3 + rand(R) * 0.25), 0.035 translate Pool + <D * sin(radians(A)) * 1.4, -0.1, D * cos(radians(A))> }
  #end
  sphere { 0, 1 scale <0.42, 0.3, 0.36> translate Pool + <0.6, 0.05, -0.2> }
  sphere { 0, 1 scale <0.3, 0.42, 0.3> translate Pool + <-0.4, 0.05, 0.4> }
  sphere { 0, 1 scale <0.5, 0.22, 0.4> translate Pool + <-0.1, 0.05, -0.75> }
  texture { pigment { rgb 0.1 } finish { diffuse 0.7 specular 0.4 roughness 0.02 } }
}

union {
  #for (X, -6, 6)
    #for (Z, -3, 4)
      #local P = <X * 0.75 + (rand(R) - 0.5) * 0.3, 0, Z * 0.75 + (rand(R) - 0.5) * 0.3>;
      #if (vlength(P - Geode * <1, 0, 1>) > 1.4 & vlength((P - Pool) / <1.8, 1, 1.4>) > 1.1 & vlength(P - Mid * <1, 0, 1>) > 0.8)
        #local H = 0.3 + rand(R) * 0.9;
        #switch (mod(abs(X + Z), 3))
          #case (0) cylinder { P, P + y * H, 0.04 } #break
          #case (1) union { cylinder { P, P + y * H, 0.025 } sphere { P + y * (H + 0.08), 0.1 } } #break
          #case (2) cone { P, 0.09, P + y * H, 0.01 } #break
        #end
      #end
    #end
  #end
  union {
    box { <-0.4, 0, -0.4>, <0.4, 0.8, 0.4> }
    sphere { y * 1.15, 0.35 }
    translate <-1.8, 0, 2.3>
  }
  difference {
    box { <-0.9, 0, -0.15>, <0.9, 1.6, 0.15> }
    cylinder { <0, 0.8, -1>, <0, 0.8, 1>, 0.6 }
    box { <-0.6, -0.1, -1>, <0.6, 0.8, 1> }
    translate <1.7, 0, 2.6>
  }
  texture { White }
}
