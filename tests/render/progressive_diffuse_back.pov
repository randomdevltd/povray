// A lens focuses a photon-only lamp onto a thin disc seen from above (front) or below (back).
#version 3.8;
#ifndef (Below) #declare Below = 0; #end
#ifndef (Back) #declare Back = 0.8; #end
#ifndef (Front) #declare Front = 0; #end
global_settings { assumed_gamma 1 ambient_light 0 photons { method 2 quality 0.5 } }
camera { orthographic location <0, (Below ? -4 : 4), 0> look_at <0, 0, 0> right 2.4 * x up 2.4 * z sky z }
light_source { <0, 6, 0> rgb 4 photon_only on photons { refraction on reflection off } }
sphere { <0, 1.5, 0>, 0.7 pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1.5 }
  photons { target refraction on reflection off collect off } no_image }
disc { <0, 0, 0>, y, 1.2 pigment { rgb 1 } finish { ambient 0 diffuse Front, Back } }
