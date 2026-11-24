// lit_by_tags: which lights light a surface, a subsurface and a medium, chosen by the lights' tags.
// Case 1 surface and a surface light, 2 subsurface, 3 media, 4 inheritance and override, 5 a camera filter, 6 a light group.
#version 4.0;
#ifndef (Case) #declare Case = 1; #end
#ifndef (Query) #declare Query = 1; #end
#ifndef (NoB) #declare NoB = 0; #end
#ifndef (NoD) #declare NoD = 0; #end
global_settings { assumed_gamma 1 subsurface { method 2 } }
background { rgb 0 }
camera {
  location <0, 1.5, -6> look_at <0, 0.3, 0> right 4/3*x
  #if (Case = 5) filter_tags { !"b" } #end
}

light_source { <-4, 6, -5> rgb <1, 0.8, 0.6> tags { "a" } }
#if (!NoB)
  light_source { <5, 4, -3> rgb <0.4, 0.6, 1> tags { "b" } }
#end

#switch (Case)
  #case (1)
    // the floor leaves out the lamp "b" and the panel's own surface light, which answers to the panel's tags
    plane { y, -1 pigment { rgb 0.8 } finish { diffuse 1 #if (Query) lit_by_tags { "a" } #end } }
    box { <-0.5, 1.5, -0.5>, <0.5, 1.6, 0.5>
      pigment { rgb 1 } finish { emission 1 diffuse 0 }
      #if (!NoB) light { brightness 4 samples 4 } #end
      tags { "panel" }
    }
  #break
  #case (2)
    // the surface takes only "a"; Query 1 lets the subsurface take "b" as well, Query 0 keeps it to "a"
    sphere { 0, 1
      pigment { rgb <0.9, 0.7, 0.6> }
      finish { diffuse 0.6 lit_by_tags { "a" }
        subsurface { translucency <0.8, 0.5, 0.3> #if (Query) lit_by_tags { "a" | "b" } #else lit_by_tags { "a" } #end } }
    }
  #break
  #case (3)
    // a scattering box that scatters only "a"; its hollow surface is invisible
    box { -1, 1 pigment { rgbt 1 } hollow
      interior { media { method 3 intervals 1 samples 4 scattering { 1, 0.6 } #if (Query) lit_by_tags { "a" } #end } }
    }
  #break
  #case (4)
    // Query 1: the left sphere inherits the union's "a", the right overrides it with "b"; Query 0 writes both per finish
    union {
      sphere { -1.1*x, 1 pigment { rgb 1 } #if (!Query) finish { lit_by_tags { "a" } } #end }
      sphere { 1.1*x, 1 pigment { rgb 1 } finish { lit_by_tags { "b" } } }
      #if (Query) lit_by_tags { "a" } #end
    }
  #break
  #case (5)
    // the query selects "b", which the camera's filter leaves out: the sphere is unlit, as by a query of no light
    sphere { 0, 1 pigment { rgb 1 } #if (Query) lit_by_tags { "b" } #else lit_by_tags { any & none } #end }
  #break
  #case (6)
    // a group member lit only where group and query agree: the query leaves out the group's lamp "d" and the global "b"
    light_group {
      light_source { <0, 5, -4> rgb <0.5, 1, 0.5> tags { "c" } }
      #if (!NoD) light_source { <-3, 2, -4> rgb <1, 0.4, 0.4> tags { "d" } } #end
      sphere { 0, 1 pigment { rgb 1 } #if (Query) lit_by_tags { "a" | "c" } #end }
      global_lights on
    }
  #break
#end
