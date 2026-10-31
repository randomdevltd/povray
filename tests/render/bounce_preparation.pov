// bounce_preparation.pov: Case 0 passive walls, a point light and a panel; 1 a scattering volume light, an area light;
// 2 a subsurface block; 3 a mesh; 4 object-testing density, subsurface, jittered area light, screen-pigment blocker;
// 5 a thin subsurface shell lit from inside and out; 6 a union of two subsurface materials and a diffuse part; 7 a
// point light with inverse-square falloff on the centre node of a scattering volume light, or Offset off it.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Sources) #declare Sources = 6; #end
#ifndef (Samples) #declare Samples = 8; #end
#ifndef (Passes) #declare Passes = 2; #end
#ifndef (Offset) #declare Offset = 0; #end
global_settings { assumed_gamma 1
  #if (Case = 2 | Case = 4 | Case = 5 | Case = 6) subsurface { method 2 samples 16, 4 spacing 0.5 } #end
}
#if (Case != 7) camera { location <0, 1.5, -3.2> look_at <0, 1, 0> angle 70 } #end
background { rgb 0 }
#macro Wall(Corner1, Corner2, Colour)
  box { Corner1, Corner2
    pigment { rgb Colour } finish { ambient 0 emission 0 diffuse 0.8 }
    light { bounce { passes Passes } samples Samples }
  }
#end

#if (Case = 0)
  light_source { <0.6, 2.2, -0.8>, rgb 3 fade_distance 1 fade_power 2 }
  box { <-0.4, 2.48, -0.4>, <0.4, 2.5, 0.4>
    pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0.5 }
    light { brightness 4 nits samples Samples bounce { passes Passes } }
  }
  #declare Walls = array[6] {
    <0.8, 0.8, 0.8>, <0.9, 0.2, 0.2>, <0.2, 0.9, 0.2>, <0.8, 0.8, 0.8>, <0.2, 0.3, 0.9>, <0.9, 0.9, 0.3>
  }
  #for (I, 0, 5)
    #local Colour = Walls[I];
    #if (I < Sources)
      #switch (I)
        #case (0) Wall(<-1.5, -0.1, -1.5>, <1.5, 0, 1.5>, Colour) #break
        #case (1) Wall(<-1.6, 0, -1.5>, <-1.5, 2.5, 1.5>, Colour) #break
        #case (2) Wall(<1.5, 0, -1.5>, <1.6, 2.5, 1.5>, Colour) #break
        #case (3) Wall(<-1.5, 0, 1.5>, <1.5, 2.5, 1.6>, Colour) #break
        #case (4) Wall(<-1.5, 2.5, -1.5>, <1.5, 2.6, 1.5>, Colour) #break
        #case (5) Wall(<-0.3, 0, -0.3>, <0.3, 0.6, 0.3>, Colour) #break
      #end
    #else
      #switch (I)
        #case (0) box { <-1.5, -0.1, -1.5>, <1.5, 0, 1.5> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
        #case (1) box { <-1.6, 0, -1.5>, <-1.5, 2.5, 1.5> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
        #case (2) box { <1.5, 0, -1.5>, <1.6, 2.5, 1.5> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
        #case (3) box { <-1.5, 0, 1.5>, <1.5, 2.5, 1.6> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
        #case (4) box { <-1.5, 2.5, -1.5>, <1.5, 2.6, 1.5> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
        #case (5) box { <-0.3, 0, -0.3>, <0.3, 0.6, 0.3> pigment { rgb Colour } finish { ambient 0 diffuse 0.8 } } #break
      #end
    #end
  #end
#end

#if (Case = 1)
  light_source { <0, 2.4, 0>, rgb 2 area_light x * 0.4, z * 0.4, 3, 3 jitter fade_distance 1 fade_power 2 }
  Wall(<-1.5, -0.1, -1.5>, <1.5, 0, 1.5>, <0.8, 0.6, 0.4>)
  sphere { <0, 1, 0>, 0.6 pigment { rgbt 1 } hollow
    interior {
      media { method 4 resolution 0.08 scattering { 1, rgb 0.6 } absorption rgb 0.2
        density { spherical color_map { [0 rgb 0] [1 rgb 1] } scale 0.6 } }
      light { brightness 1 samples Samples bounce { passes Passes } }
    }
  }
#end

#if (Case = 2)
  light_source { <0.5, 2.5, -1>, rgb 4 fade_distance 1 fade_power 2 }
  plane { y, 0 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
  box { <-0.5, 0, -0.5>, <0.5, 0.8, 0.5>
    pigment { rgb <0.9, 0.6, 0.5> } finish { ambient 0 emission 0 diffuse 0.8 subsurface { translucency 0.4 } }
    interior { ior 1.3 }
    light { bounce { passes Passes } samples Samples }
  }
#end

#if (Case = 3)
  light_source { <0.5, 2.5, -1>, rgb 4 fade_distance 1 fade_power 2 }
  #declare N = 40;
  #declare Warm = texture { pigment { rgb <0.8, 0.5, 0.3> } finish { ambient 0 emission 0 diffuse 0.8 } };
  #declare Cool = texture { pigment { rgb <0.3, 0.5, 0.8> } finish { ambient 0 emission 0 diffuse 0.8 } };
  mesh {
    #for (I, 0, N - 1) #for (J, 0, N - 1)
      #local A = <I / N * 2 - 1, 0.1 * sin(I + J), J / N * 2 - 1>;
      #local B = <(I + 1) / N * 2 - 1, 0.1 * sin(I + J + 1), J / N * 2 - 1>;
      #local C = <I / N * 2 - 1, 0.1 * sin(I + J + 1), (J + 1) / N * 2 - 1>;
      #local D = <(I + 1) / N * 2 - 1, 0.1 * sin(I + J + 2), (J + 1) / N * 2 - 1>;
      triangle { A, B, C texture { Warm } }
      triangle { B, D, C texture { Cool } }
    #end #end
    light { bounce { passes Passes } samples Samples }
  }
#end

#if (Case = 4)
  light_source { <0, 2.4, -0.5>, rgb 2 area_light x * 0.4, z * 0.4, 3, 3 jitter fade_distance 1 fade_power 2 }
  #declare Core = sphere { <0.6, 1, 0>, 0.25 };
  #declare CoreDensity = function { pigment { object { Core rgb 0.2 rgb 1 } } };
  sphere { <0.6, 1, 0>, 0.45 pigment { rgbt 1 } hollow
    interior {
      media { method 3 scattering { 1, rgb 0.6 }
        density { function { CoreDensity(x, y, z).red } } }
      light { brightness 1 samples Samples bounce { passes Passes } }
    }
  }
  box { <-0.5, 0, -0.5>, <0.1, 0.8, 0.1>
    pigment { rgb <0.9, 0.6, 0.5> } finish { ambient 0 emission 0 diffuse 0.8 subsurface { translucency 0.4 } }
    interior { ior 1.3 }
    light { bounce { passes Passes } samples Samples }
  }
  #declare Peek = camera { location <0, 3, -2> look_at <0, 0.5, 0> };
  box { <-0.6, 1.6, -0.6>, <0.2, 1.65, 0.2>
    pigment { screen { camera { Peek } fallback { rgb 0.5 } } } finish { ambient 0 diffuse 0.8 }
  }
  Wall(<-1.5, -0.1, -1.5>, <1.5, 0, 1.5>, <0.8, 0.6, 0.4>)
#end

#if (Case = 5)
  light_source { <0.8, 2.4, -1>, rgb 2 fade_distance 1 fade_power 2 }
  light_source { <0, 0.6, 0>, rgb 0.5 }
  plane { y, 0 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
  difference {
    sphere { <0, 0.6, 0>, 0.6 }
    sphere { <0, 0.6, 0>, 0.55 }
    plane { -y, -0.3 }
    pigment { rgb <0.8, 0.6, 0.9> } finish { ambient 0 emission 0 diffuse 0.6 subsurface { translucency 0.4 } }
    interior { ior 1.36 }
    light { bounce { passes 1 } samples Samples }
  }
#end

#if (Case = 6)
  light_source { <0.8, 2.4, -1>, rgb 2 fade_distance 1 fade_power 2 }
  plane { y, 0 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
  union {
    sphere { <-0.35, 0.4, 0>, 0.4
      pigment { rgb <0.9, 0.6, 0.5> } finish { ambient 0 emission 0 diffuse 0.6 subsurface { translucency 0.4 } }
      interior { ior 1.36 }
    }
    sphere { <0.35, 0.4, 0>, 0.4
      pigment { rgb <0.5, 0.7, 0.9> } finish { ambient 0 emission 0 diffuse 0.6 subsurface { translucency 0.1 } }
      interior { ior 1.5 }
    }
    box { <-0.08, 0, -0.6>, <0.08, 0.15, 0.6> pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 } }
    light { bounce { passes 1 } samples Samples }
  }
#end

#if (Case = 7)
  light_source { <Offset, 0, 0>, rgb 1 fade_power 2 media_interaction on media_attenuation on }
  sphere { 0, 1 pigment { rgbt 1 } hollow
    interior {
      media { method 4 resolution 0.1 scattering { 5, rgb 0.5 eccentricity 0.6 } }
      light { samples Samples bounce { passes 1 } }
    }
  }
  camera { location <0, 0.5, -3> look_at 0 angle 50 }
#end
