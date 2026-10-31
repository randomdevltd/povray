// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// The cubic superellipsoids of superel1.pov (top three rows) and the cylindrical ones of superel2.pov (bottom three rows), exponent 0.9 to 0.1 from left to right: in each set the native superellipsoid sits above the same shape drawn as an f_superellipsoid isosurface, and below them that isosurface is coloured by its function at each hit, white on the surface turning red towards four times the accuracy. The columns cycle through plain, reflective, glass with photon caustics, bumped and scattering-media finishes, all under an area light with radiosity. +w640 +h480 +a0.3
#version 3.7;
#include "functions.inc"

#declare Acc = 0.0005;

global_settings {
  assumed_gamma 1.0
  radiosity { count 80 error_bound 0.5 recursion_limit 1 }
  photons { spacing 0.01 }
}
#default { finish { ambient 0.1 diffuse 0.9 } }

camera {
  orthographic
  location <0, 0, -10>
  right 48 * x
  up 36 * y
  look_at <0, 0, 0>
}

light_source { <50, 50, -100> color rgb 0.7
  area_light <12, 0, 0>, <0, 12, 0>, 6, 6 adaptive 1 jitter circular orient
  photons { refraction on reflection on }
}
light_source { <0, 0, -10000> color rgb 0.7 }

background { color rgb 0.35 }
plane { z, 2 hollow
  pigment { checker color rgb <0.55, 0.7, 0.45> color rgb <0.2, 0.3, 0.55> }
  finish { ambient 0.1 diffuse 0.4 }
}

#declare Red = texture { pigment { color rgb <1, 0, 0> } finish { ambient 0.2 diffuse 0.4 phong 0.5 phong_size 5 } }
#declare Glass = texture {
  pigment { color rgbf <1, 0.7, 0.7, 0.9> }
  finish { ambient 0 diffuse 0.1 specular 0.6 roughness 0.005 reflection 0.1 }
}

#macro Surface(E, N, Look)
  #switch (Look)
    #case (1) texture { Red finish { reflection 0.35 } } #break
    #case (2) texture { Glass } interior { ior 1.5 } photons { target refraction on reflection on } #break
    #case (3) texture { Red normal { bumps 0.6 scale 0.15 } } #break
    #case (4)
      texture { pigment { color rgbt 1 } finish { ambient 0 diffuse 0 } }
      interior { media { scattering { 1, color rgb <0.9, 0.3, 0.3> } } }
      hollow
    #break
    #case (5)
      texture {
        pigment { function { abs(f_superellipsoid(x, y, z, E, N)) / (4 * Acc) } color_map { [0 rgb 1] [1 rgb <1, 0, 0>] } }
        finish { ambient 0.1 diffuse 0.9 }
      }
    #break
    #else texture { Red }
  #end
#end

#macro Shape(E, N, Iso, Look, Rot, Col, Row)
  #if (Iso)
    isosurface {
      function { -f_superellipsoid(x, y, z, E, N) }
      contained_by { box { -1.001, 1.001 } }
      max_gradient 1
      accuracy Acc
  #else
    superellipsoid { <E, N>
  #end
      Surface(E, N, Look)
      scale 2 rotate Rot translate <Col, Row, 0>
    }
#end

#declare Exponent = array[9] { 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1 }
#for (I, 0, 8)
  #local Col = 5 * (I - 4);
  #local Look = mod(I, 5);
  Shape(Exponent[I], Exponent[I], 0, Look, <-15, 30, 0>, Col, 13.25)
  Shape(Exponent[I], Exponent[I], 1, Look, <-15, 30, 0>, Col, 8.25)
  Shape(Exponent[I], Exponent[I], 1, 5, <-15, 30, 0>, Col, 3.25)
  Shape(1.0, Exponent[I], 0, Look, <-105, 30, 0>, Col, -3.25)
  Shape(1.0, Exponent[I], 1, Look, <-105, 30, 0>, Col, -8.25)
  Shape(1.0, Exponent[I], 1, 5, <-105, 30, 0>, Col, -13.25)
#end
