// Subsurface in a wide, busy view: a 360-degree spherical camera, 13 lights (two soft area suns), about 20000 small
// objects, bark-like mesh2 trunks and a small smooth mesh with subsurface, each covering few pixels; see doc/PERF.md.
#version 3.7;
#ifndef (Diffuse) #declare Diffuse = 6; #end
#ifndef (Single) #declare Single = 3; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
#ifndef (Method) #declare Method = 1; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 80
  #if (Subsurface) subsurface { samples Diffuse, Single method Method } #end
}

camera { spherical angle 360 180 location <0, 1.6, 0> look_at <0, 1.6, 1> }

light_source {
  <-300, 400, -200>, rgb <1.0, 0.9, 0.8> * 1.3
  parallel point_at <0, 0, 0>
  area_light <10, 0, 0>, <0, 0, 10>, 5, 5
  adaptive 1 jitter
}
light_source {
  <250, 300, 300>, rgb <0.6, 0.7, 1.0> * 0.5
  parallel point_at <0, 0, 0>
  area_light <10, 0, 0>, <0, 0, 10>, 5, 5
  adaptive 1 jitter
}
#declare Seed = seed(7);
#for (I, 0, 10)
  #declare A = I * 32.7;
  #declare P = <sin(radians(A)) * (3 + 4 * rand(Seed)), 0.4 + 2.5 * rand(Seed), cos(radians(A)) * (3 + 4 * rand(Seed))>;
  #if (mod(I, 3) = 0)
    light_source { P, rgb <1, 0.8, 0.6> * 0.4 spotlight point_at <P.x * 0.5, 0, P.z * 0.5> radius 25 falloff 40 }
  #else
    light_source { P, rgb <1, 0.85, 0.7> * 0.35 fade_distance 2 fade_power 2 }
  #end
#end

sky_sphere { pigment { gradient y color_map { [0 rgb <0.7, 0.75, 0.8>] [1 rgb <0.2, 0.35, 0.7>] } } }
plane { y, 0 pigment { rgb <0.35, 0.3, 0.25> } }

// Clutter: many small finite objects that every shadow ray has to be tested against.
#for (I, 0, 19999)
  #declare R = 2 + 28 * sqrt(rand(Seed));
  #declare A = 360 * rand(Seed);
  #declare S = 0.03 + 0.08 * rand(Seed);
  #if (rand(Seed) < 0.5)
    sphere { <sin(radians(A)) * R, S, cos(radians(A)) * R>, S pigment { rgb <0.3, 0.45, 0.2> } }
  #else
    box { -S, S rotate y * 360 * rand(Seed) translate <sin(radians(A)) * R, S, cos(radians(A)) * R> pigment { rgb <0.4, 0.35, 0.3> } }
  #end
#end

// A trunk: a closed mesh2 cylinder with ridged, noisy bark, NA segments around by NH along.
#macro Trunk(Radius, Height, NA, NH)
  #local C = NA * (NH + 1);
  mesh2 {
    vertex_vectors { C + 2,
      #for (J, 0, NH) #for (K, 0, NA - 1)
        #local T = 2 * pi * K / NA; #local Y = Height * J / NH;
        #local Rr = Radius * (1 + 0.06 * sin(9 * T + 0.7 * Y) + 0.03 * sin(31 * T - 2.3 * Y) - 0.1 * J / NH);
        <Rr * cos(T), Y, Rr * sin(T)>,
      #end #end
      <0, 0, 0>, <0, Height, 0>
    }
    face_indices { 2 * NA * NH + 2 * NA,
      #for (J, 0, NH - 1) #for (K, 0, NA - 1)
        #local A0 = J * NA + K; #local A1 = J * NA + mod(K + 1, NA);
        <A0, A1, A1 + NA>, <A0, A1 + NA, A0 + NA>,
      #end #end
      #for (K, 0, NA - 1) <C, K, mod(K + 1, NA)>, <C + 1, NH * NA + K, NH * NA + mod(K + 1, NA)>, #end
    }
    inside_vector y
  }
#end
#declare Bark = texture {
  pigment { rgb <0.55, 0.42, 0.3> }
  finish { diffuse 0.7 subsurface { translucency <1.2, 0.8, 0.5> } }
}
#declare TrunkMesh = Trunk(0.35, 5, 160, 120)
#for (I, 0, 5)
  #declare A = 60 * I + 20;
  object { TrunkMesh texture { Bark } interior { ior 1.4 } rotate y * 37 * I translate <sin(radians(A)) * (4 + I), 0, cos(radians(A)) * (4 + I)> }
#end

// A small smooth mesh: a squashed sphere of smooth triangles.
#declare Skin = texture {
  pigment { rgb <0.9, 0.65, 0.5> }
  finish { diffuse 0.7 subsurface { translucency <2, 1, 0.6> } }
}
#declare NU = 48; #declare NV = 24;
mesh2 {
  vertex_vectors { (NU + 1) * (NV + 1),
    #for (J, 0, NV) #for (K, 0, NU) #local T = 2 * pi * K / NU; #local F = pi * J / NV;
      <sin(F) * cos(T), cos(F), sin(F) * sin(T)>,
    #end #end
  }
  normal_vectors { (NU + 1) * (NV + 1),
    #for (J, 0, NV) #for (K, 0, NU) #local T = 2 * pi * K / NU; #local F = pi * J / NV;
      <sin(F) * cos(T), cos(F), sin(F) * sin(T)>,
    #end #end
  }
  face_indices { 2 * NU * NV,
    #for (J, 0, NV - 1) #for (K, 0, NU - 1)
      #local A0 = J * (NU + 1) + K;
      <A0, A0 + 1, A0 + NU + 2>, <A0, A0 + NU + 2, A0 + NU + 1>,
    #end #end
  }
  inside_vector y
  texture { Skin } interior { ior 1.4 }
  scale <0.25, 0.18, 0.2> translate <1.2, 0.9, 2>
}
