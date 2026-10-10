// media_light.pov: emitting media as lights over a white floor seen from straight above; media_light.sh names the cases.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (N) #declare N = 16; #end
#declare Height = 2;
#declare Radius = 0.25;
#declare Emission = 50;
#declare Ball = sphere { 0, Radius pigment { rgbt 1 } hollow no_image }

global_settings {
  assumed_gamma 1
  #if (Case = 3 | Case = 4)
    radiosity { count 200 nearest_count 1 error_bound 0.2 recursion_limit 1 media on brightness 1 }
  #end
}
camera { orthographic location <0, 10, 0> sky z look_at 0 right x * 4 up z * 4 }
background { rgb 0 }

#declare Floor = box { <-2, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
#declare LeftFloor = box { <-2, -1, -2>, <0, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
#declare RightFloor = box { <0, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }

#macro EmittingBall(R, E, Samples, Light)
  object {
    Ball scale R / Radius
    interior { media { method 4 emission E #if (Light) light_source { samples Samples #ifdef (Photons) photons { refraction on target } #end #ifdef (Shadowless) shadowless #end } #end } }
    translate y * Height
  }
#end

#switch (Case)
  #case (1) object { Floor } EmittingBall(Radius, Emission, 16, 1) #break
  #case (2)
    object { Floor }
    light_source { y * Height, rgb Emission * 4 / 3 * pi * pow(Radius, 3) fade_distance 0 fade_power 2 }
  #break
  #case (3) object { Floor } EmittingBall(Radius, Emission, 16, 1) #break
  #case (4) object { Floor } EmittingBall(Radius, Emission, 16, 0) #break
  #case (5)
    light_group { object { LeftFloor } EmittingBall(Radius, Emission, 16, 1) global_lights off }
    object { RightFloor }
  #break
  #case (6)
    object { Floor }
    EmittingBall(0.05, Emission * 125, 16, 1)
    sphere { y * Height, 0.5 pigment { rgbt 1 } hollow no_image interior { media { absorption 1 } } }
  #break
  #case (7) object { Floor } EmittingBall(0.05, Emission * 125, 16, 1) #break
  #case (8) object { Floor } EmittingBall(Radius, Emission, 64, 1) #break
  #case (9)
    object { Floor }
    sphere {
      0, 0.5 pigment { rgbt 1 } hollow no_image
      interior { media { method 4 emission 6 density { spherical color_map { [0 rgb 0] [0.7 rgb 0.2] [1 rgb 3] } } light_source { samples N } } }
      translate <0.4, Height, 0.2>
    }
  #break
  #case (10)
    object { Floor }
    sphere {
      0, 0.5 pigment { rgbt 1 } hollow no_image
      interior { media { method 4 emission rgb <6, 3, 1.5> density { gradient x color_map { [0 rgb <0, 0.2, 1>] [1 rgb <1, 0.4, 0>] } translate -x * 0.5 } light_source { samples N } } }
      translate y * Height
    }
  #break
  #case (11) #case (12) #case (13)
    object { Floor }
    EmittingBall(Radius, Emission, N, 1)
    #if (Case != 13) box { <-0.25, 0.99, -0.25>, <0.25, 1, 0.25> no_image pigment { rgb 0.5 } } #end
  #break
  #case (14)
    object { Floor }
    #declare Lamp = object { EmittingBall(Radius, Emission, N, 1) }
    object { Lamp scale 0.5 translate <0.5, 0, 0.3> }
  #break
  #case (15)
    object { Floor }
    object { Ball scale 0.5 interior { media { method 4 emission Emission light_source { samples N } } } translate <0.5, Height / 2, 0.3> }
  #break
  #case (18)
    object { Floor }
    isosurface_mesh {
      function { sqrt(x * x + y * y + z * z) - Radius } contained_by { sphere { 0, Radius * 1.2 } }
      pigment { rgbt 1 } hollow no_image
      interior { media { method 4 emission Emission light_source { samples N } } }
      translate y * Height
    }
  #break
  #case (19) #case (20)
    object { Floor }
    #declare Gem = mesh2 {
      povm "media_light.povm" inside_vector <0.123, 0.937, 0.271>
      pigment { rgbt 1 } hollow interior { media { method 4 emission 0.3 light_source { samples N } } }
      scale 0.5
    }
    #if (Case = 19) union { object { Gem } sphere { <1.5, 0, 1.5>, 0.05 no_image no_shadow } split_union off translate y * Height }
    #else object { Gem translate y * Height }
    #end
  #break
  #case (16) #case (17)
    object { Floor }
    box { <-2, 0.01, -2>, <2, 3, 2> pigment { rgbt 1 } hollow interior { media { scattering { 1, 0.08 } } } }
    sphere {
      0, 0.5 pigment { rgbt 1 } hollow
      interior { media { method 4 emission 0.5 #if (Case = 17) scattering { 1, 1e-6 } #end density { spherical } light_source { samples N } } }
      translate y * 1.2
    }
  #break
#end
