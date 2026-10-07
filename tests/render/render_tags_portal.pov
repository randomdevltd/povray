#version 4.0;
#ifndef (Mode) #declare Mode = 0; #end
global_settings { assumed_gamma 1 }
#if (Mode < 3) global_settings { filter_tags { "hidden" } } #end
#if (Mode = 3) global_settings { filter_tags { "far" } } #end

camera {
  orthographic
  location <0, 0, -5>
  look_at 0
  right 3*x
  up 3*y
  filter_tags { "near" }
}

background { color rgb 0 }

sphere {
  <10, 0, 2>, 1
  pigment { color rgb <0, 0, 1> }
  finish { emission 1 diffuse 0 }
  tags { "far" }
}

portal {
  polygon { 5, <-1, -1, 0>, <1, -1, 0>, <1, 1, 0>, <-1, 1, 0>, <-1, -1, 0> }
  to { translate 10*x }
  far off
  #if (Mode < 2) filter_tags { "hidden" } #end
  #if (Mode = 2) filter_tags { "far" } #end
  near {
    front on back on
    #if (Mode = 0)
      filter_tags { "hidden" }
      front_filter_tags { "far" } back_filter_tags { "far" }
    #end
    #if (Mode = 1) filter_tags { "far" } #end
    #if (Mode = 5) filter_tags { "hidden" } #end
  }
  tags { "near" }
}
