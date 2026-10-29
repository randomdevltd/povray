// An unbounded constant field has an analytic finite camera optical depth.
#version 3.8;
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
media { method 4 absorption rgb <0.2,0.4,0.6> }
plane { z,1 hollow pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 } }
