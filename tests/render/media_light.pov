// media_light.pov: an emitting ball whose medium is a light, over a white floor seen from straight above; cases in
// media_light.sh: 1 ball, 2 point light, 3/4 radiosity with/without the light, 5 light group, 6/7 smoke, 8 64 samples.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
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
    interior { media { method 4 emission E #if (Light) light_source { samples Samples #ifdef (Photons) photons { refraction on } #end } #end } }
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
#end
