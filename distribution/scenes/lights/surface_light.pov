// Surface lights on a sphere, a patterned box, a strip and a mesh, stated in nits, lumens and their own emission. +w640 +h480
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <4,3,-6> look_at <0,1,0> right x*image_width/image_height }
background { rgb 0.01 }
plane { y,0 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
sphere { <-1.5,1.7,0>,0.25
  pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
  light { brightness 45 nits samples 32 }
}
box { <-0.6,1,-0.05>,<0.6,2,0.05>
  pigment { checker rgb <0.1,0.3,1> rgb <1,0.15,0.02> scale 0.3 }
  finish { ambient 0 emission 1 diffuse 0 }
  light { emission brightness 30 nits samples 32 }
  rotate y*15
}
box { <-1,2.6,-0.05>,<1,2.65,0.05>
  pigment { rgb <1,0.7,0.25> } finish { ambient 0 emission 1 diffuse 0 }
  light { pigment { rgb <1,0.7,0.25> } brightness 120 lumens samples 32 }
  translate z*-0.5
}
mesh {
  triangle { <1,1,-0.3>,<2,1,0.7>,<2,1,-0.3> }
  triangle { <1,1,-0.3>,<1,1,0.7>,<2,1,0.7> }
  pigment { rgb <0.15,1,0.35> } finish { ambient 0 emission 1 diffuse 0 }
  light { emission brightness 18 nits samples 32 }
}
sphere { <0,0.35,-1>,0.35 pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
