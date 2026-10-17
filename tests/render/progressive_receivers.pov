// A glass ball focuses a photon-only lamp onto a wavy floor, built as each kind of receiver in turn.
#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Receiver) #declare Receiver = 0; #end  // 0 mesh, 1 smooth mesh, 2 height_field, 3 smooth height_field, 4 isosurface, 5 parametric
#ifndef (Accuracy) #declare Accuracy = 0.001; #end
#ifndef (Count) #declare Count = 400000; #end
global_settings { assumed_gamma 1 photons { #if (Method = 2) method 2 quality 1 #else count Count #end } }
camera { orthographic location <0, 10, 0> look_at <0, 0, 0> right 4 * x up 3 * z }
light_source { <0, 8, 0> rgb 0.5 fade_power 2 fade_distance 0.70710678 photon_only on photons { refraction on reflection off } }
sphere { <0, 1.5, 0>, 1 pigment { rgbf 1 } interior { ior 1.5 } photons { target } no_shadow no_image }
#declare N = 24;
#macro P(I, J) <-2 + 4 * I / N, 0.08 * sin(4 * pi * I / N) * sin(4 * pi * J / N), -1.5 + 3 * J / N> #end
#macro Nm(I, J) vnormalize(<-0.08 * pi * cos(4 * pi * I / N) * sin(4 * pi * J / N), 1, -0.08 * pi * 4 / 3 * sin(4 * pi * I / N) * cos(4 * pi * J / N)>) #end
#switch (Receiver)
#range (0, 1)
  #declare Floor = mesh { #for (I, 0, N - 1) #for (J, 0, N - 1)
    #if (Receiver = 1)
      smooth_triangle { P(I, J), Nm(I, J), P(I + 1, J), Nm(I + 1, J), P(I + 1, J + 1), Nm(I + 1, J + 1) }
      smooth_triangle { P(I, J), Nm(I, J), P(I + 1, J + 1), Nm(I + 1, J + 1), P(I, J + 1), Nm(I, J + 1) }
    #else
      triangle { P(I, J), P(I + 1, J), P(I + 1, J + 1) } triangle { P(I, J), P(I + 1, J + 1), P(I, J + 1) }
    #end
  #end #end }
#break
#range (2, 3)
  #declare Floor = height_field { function 48, 48 { 0.5 + 0.5 * sin(x * 25) * sin(y * 25) } #if (Receiver = 3) smooth #end
    translate <-0.5, 0, -0.5> scale <4, 0.08, 3> }
#break
#case (4)
  #declare Floor = isosurface { function { y - 0.02 * sin(3 * x) * sin(3 * z) } contained_by { box { <-2, -0.1, -1.5>, <2, 0.1, 1.5> } }
    max_gradient 1.1 accuracy Accuracy }
#break
#case (5)
  #declare Floor = parametric { function { u } function { 0.02 * sin(3 * u) * sin(3 * v) } function { v } <-2, -1.5>, <2, 1.5>
    contained_by { box { <-2, -0.1, -1.5>, <2, 0.1, 1.5> } } precompute 12 x, y, z accuracy Accuracy }
#break
#end
object { Floor pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
