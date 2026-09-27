// Subsurface on open, thin, partly see-through leaf sheets: fronds of curved mesh2 leaflets (no inside) under two soft area
// suns and eleven lamps and spots; see doc/PERF.md.
#version 3.7;
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 12; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
#ifndef (Method) #declare Method = 1; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 80
  #if (Subsurface) subsurface { samples Diffuse, Single method Method } #end
}

camera { location <0, 0.6, -2.6> look_at <0, 1.3, 0> angle 55 right x * 4 / 3 }

light_source {
  <-300, 400, 200>, rgb <1.0, 0.9, 0.8> * 1.3
  parallel point_at <0, 0, 0>
  area_light <10, 0, 0>, <0, 0, 10>, 5, 5
  adaptive 1 jitter
}
light_source {
  <250, 300, -300>, rgb <0.6, 0.7, 1.0> * 0.5
  parallel point_at <0, 0, 0>
  area_light <10, 0, 0>, <0, 0, 10>, 5, 5
  adaptive 1 jitter
}
#declare Seed = seed(11);
#for (I, 0, 10)
  #declare A = I * 32.7;
  #declare P = <sin(radians(A)) * (1.5 + 2 * rand(Seed)), 0.3 + 2.5 * rand(Seed), cos(radians(A)) * (1.5 + 2 * rand(Seed))>;
  #if (mod(I, 3) = 0)
    light_source { P, rgb <1, 0.8, 0.6> * 0.4 spotlight point_at <0, 1.2, 0> radius 25 falloff 40 }
  #else
    light_source { P, rgb <1, 0.85, 0.7> * 0.35 fade_distance 2 fade_power 2 }
  #end
#end

sky_sphere { pigment { gradient y color_map { [0 rgb <0.7, 0.75, 0.8>] [1 rgb <0.2, 0.35, 0.7>] } } }
plane { y, 0 pigment { rgb <0.35, 0.3, 0.25> } }

#declare Leaf = texture {
  pigment { bozo scale 0.05 color_map { [0 rgbf <0.25, 0.55, 0.15, 0.3>] [1 rgbf <0.45, 0.75, 0.25, 0.35>] } }
  finish { diffuse 0.6, 0.5 specular 0.4 roughness 0.02 #if (Subsurface) subsurface { translucency <0.45, 1, 0.55> } #end }
}

// A leaflet: an open, curved, pointed sheet along +z of length L, NL segments long by NW across.
#macro Leaflet(L, W, NL, NW)
  mesh2 {
    vertex_vectors { (NL + 1) * (NW + 1),
      #for (J, 0, NL) #for (K, 0, NW)
        #local S = J / NL; #local T = 2 * K / NW - 1;
        #local Half = W * sin(pi * S) * (1 - 0.3 * S);
        <T * Half, -0.15 * L * S * S + 0.08 * W * T * T, L * S>,
      #end #end
    }
    face_indices { 2 * NL * NW,
      #for (J, 0, NL - 1) #for (K, 0, NW - 1)
        #local A0 = J * (NW + 1) + K;
        <A0, A0 + 1, A0 + NW + 2>, <A0, A0 + NW + 2, A0 + NW + 1>,
      #end #end
    }
  }
#end
#declare Blade = Leaflet(0.22, 0.05, 10, 4)

// Fronds: pairs of leaflets along drooping stems, around three stalks.
#for (S, 0, 2)
  #declare Base = <(S - 1) * 0.9, 0, 0.3 * S>;
  cylinder { Base, Base + y * 1.2, 0.02 pigment { rgb <0.3, 0.25, 0.15> } }
  #for (F, 0, 7)
    #declare Turn = 45 * F + 20 * rand(Seed);
    #declare Rise = 0.8 + 0.4 * rand(Seed);
    union {
      #for (K, 0, 9)
        #declare At = <0, -0.02 * K * K / 9, 0.07 * K>;
        object { Blade rotate <-10, 70, 20> translate At }
        object { Blade rotate <-10, -70, -20> translate At }
      #end
      texture { Leaf }
      rotate x * -20 rotate y * Turn translate Base + y * Rise
    }
  #end
#end
