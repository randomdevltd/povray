#version 4.0;

#ifndef (View) #declare View = 0; #end

#if (View = 1)
global_settings { assumed_gamma 1 filter_tags { "main" } }
#else
global_settings { assumed_gamma 1 }
#end

camera {
  orthographic
  location <0, 0, -8>
  look_at 0
  right 6*x
  up 2*y
  #if (View = 2) filter_tags { "alternate" } #end
}

background { color rgb 0 }

sphere {
  <-2, 0, 0>, 0.8
  pigment { color rgb <1, 0, 0> }
  finish { emission 1 diffuse 0 }
  tags { "main", "shared" }
}

sphere {
  <0, 0, 0>, 0.8
  pigment { color rgb <0, 1, 0> }
  finish { emission 1 diffuse 0 }
}

sphere {
  <2, 0, 0>, 0.8
  pigment { color rgb <0, 0, 1> }
  finish { emission 1 diffuse 0 }
  tags { "alternate", "shared" }
}
