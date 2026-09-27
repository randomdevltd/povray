// This work is licensed under the Creative Commons Attribution 3.0 Unported License.
// To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/
// or send a letter to Creative Commons, 444 Castro Street, Suite 900, Mountain View,
// California, 94041, USA.

// superel1.pov and superel2.pov as f_superellipsoid isosurfaces. Declare Set=1|2, Iso=0 (native primitive), Effect=0..7
// (reflection, glass, bumps, area light, media, radiosity, photons), Error=1 (hits shaded by |g| / 4 accuracy).

#version 3.7;
#include "functions.inc"

#ifndef (Set)    #declare Set = 1;    #end
#ifndef (Iso)    #declare Iso = 1;    #end
#ifndef (Effect) #declare Effect = 0; #end
#ifndef (Error)  #declare Error = 0;  #end
#declare Acc = 0.0005;

global_settings {
  assumed_gamma 1.0
  #if (Effect = 6) radiosity { count 80 error_bound 0.5 recursion_limit 1 } #end
  #if (Effect = 7) photons { spacing 0.01 } #end
}
#default { finish { ambient 0.1 diffuse 0.9 } }

camera {
  orthographic
  location <0, 0, -10>
  right 15 * 4/3 * x
  up 15 * y
  look_at <0, 0, 0>
}

light_source { <50, 50, -100> color rgb 0.7
  #if (Effect = 4) area_light <12, 0, 0>, <0, 12, 0>, 6, 6 adaptive 1 jitter circular orient #end
  #if (Effect = 7) photons { refraction on reflection on } #end
}
light_source { <0, 0, -10000> color rgb 0.7 }

#if (Error)
  background { color rgbt <0, 0, 0, 1> }
#else
  background { color rgb 0.35 }
#end
#if (Effect > 0 & !Error)
  plane { z, 2 hollow
    pigment { checker color rgb <0, 1, 0> color rgb <0, 0, 1> }
    finish { ambient 0.1 diffuse 0.4 }
  }
#end

#declare Red = texture { pigment { color rgb <1, 0, 0> } finish { ambient 0.2 diffuse 0.4 phong 0.5 phong_size 5 } }
#declare Glass = texture {
  pigment { color rgbf <1, 0.7, 0.7, 0.9> }
  finish { ambient 0 diffuse 0.1 specular 0.6 roughness 0.005 reflection 0.1 }
}

#macro Surface(E, N)
  #if (Error)
    texture {
      pigment { function { abs(f_superellipsoid(x, y, z, E, N)) / (4 * Acc) } color_map { [0 rgb 0] [1 rgb 1] } }
      finish { ambient 1 diffuse 0 }
    }
  #else
    #switch (Effect)
      #case (1) texture { Red finish { reflection 0.35 } } #break
      #case (2) #case (7) texture { Glass } interior { ior 1.5 } #break
      #case (3) texture { Red normal { bumps 0.6 scale 0.15 } } #break
      #case (5)
        texture { pigment { color rgbt 1 } finish { ambient 0 diffuse 0 } }
        interior { media { scattering { 1, color rgb <0.9, 0.3, 0.3> } } }
      #break
      #else texture { Red }
    #end
    #if (Effect = 7) photons { target refraction on reflection on } #end
  #end
#end

#macro Shape(E, N, Rot, Col, Row)
  #if (Iso)
    isosurface {
      function { -f_superellipsoid(x, y, z, E, N) }
      contained_by { box { -1.001, 1.001 } }
      max_gradient 1
      accuracy Acc
  #else
    superellipsoid { <E, N>
  #end
      Surface(E, N)
      #if (Effect = 5) hollow #end
      scale 2 rotate Rot translate <Col, Row, 0>
    }
#end

#declare Exponent = array[9] { 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1 }
#for (I, 0, 8)
  #if (Set = 1)
    Shape(Exponent[I], Exponent[I], <-15, 30, 0>, 5 * (mod(I, 3) - 1), 5 * (1 - div(I, 3)))
  #else
    Shape(1.0, Exponent[I], <-105, 30, 0>, 5 * (mod(I, 3) - 1), 5 * (1 - div(I, 3)))
  #end
#end
