// bounce_adaptive.pov: passive surface bounce in a small room, each case a feature the bounce nodes must resolve.
// Case 0 a hard shadow edge from a point light; 1 a thin floating occluder; 2 contact shadows; 3 a textured floor and
// wall; 4 concave and convex creases; 5 a grazing lit wall; 6 a plain box room lit inside; 7 case 0 shut in a closed
// hollow sphere, 8 in a closed box, or with Shell in a thin shell of either. S scales the scene.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (S) #declare S = 1; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Passes) #declare Passes = 1; #end
#ifndef (Inverse) #declare Inverse = 0; #end
#ifndef (Shell) #declare Shell = 0; #end
global_settings { assumed_gamma 1 ambient_light 0 max_trace_level 8 }
background { rgb 0 }
#declare T = transform { scale S };
camera { location <0, 1.1, -2.6> * S look_at <0, 0.55, 0.4> * S angle 62 right x * image_width / image_height }

#macro Passive(Colour)
  texture { pigment { rgb Colour } finish { ambient 0 emission 0 diffuse 0.8 } }
  light { emission bounce { passes Passes } samples Samples }
#end
#macro Panel(Corner1, Corner2, Nits)
  box { Corner1, Corner2 pigment { rgb 1 } finish { ambient 0 emission Nits diffuse 0 }
    light { emission brightness 1 nits samples 64 } transform { T } }
#end

#if (Case != 6)
box { <-1.5, -0.05, -1.5>, <1.5, 0, 1.5>
  #if (Case = 3)
    texture { pigment { checker rgb 0.9, rgb 0.1 scale 0.25 } finish { ambient 0 emission 0 diffuse 0.8 } }
    light { emission bounce { passes Passes } samples Samples }
  #else
    Passive(0.75)
  #end
  transform { T } }
box { <-1.5, 0, 1.5>, <1.5, 2, 1.55>
  #if (Case = 3)
    texture { pigment { checker rgb <0.9, 0.2, 0.1>, rgb 0.85 scale 0.2 } finish { ambient 0 emission 0 diffuse 0.8 } }
    light { emission bounce { passes Passes } samples Samples }
  #else
    Passive(0.75)
  #end
  transform { T } }
box { <-1.55, 0, -1.5>, <-1.5, 2, 1.5> Passive(<0.7, 0.15, 0.1>) transform { T } }
#end

#switch (Case)
#case (0)
#case (7)
#case (8)
  light_source { <1.2, 1.8, -0.6> * S, rgb 3 fade_distance S fade_power 2 }
  box { <-0.2, 0, -0.1>, <0.25, 1.3, 0.1> Passive(0.75) transform { T } }
  #declare Room = texture { pigment { rgb 0.6 } finish { ambient 0 emission 0 diffuse 0.8 } };
  #if (Case = 7)
    #if (Shell) difference { sphere { <0, 1, 0>, 3.6 } sphere { <0, 1, 0>, 3.5 } #else sphere { <0, 1, 0>, 3.5 hollow #end
      texture { Room } transform { T } }
  #end
  #if (Case = 8)
    #if (Shell) difference { box { <-3.1, -1.1, -3.1>, <3.1, 3.1, 3.1> } box { <-3, -1, -3>, <3, 3, 3> }
    #else box { <-3, -1, -3>, <3, 3, 3> #end
      texture { Room } transform { T } }
  #end
  #break
#case (1)
  Panel(<-0.4, 1.98, -0.4>, <0.4, 2, 0.4>, 12)
  box { <-0.5, 0.3, -0.5>, <0.5, 0.305, 0.5> Passive(0.75) transform { T } }
  #break
#case (2)
  Panel(<-0.4, 1.98, -0.4>, <0.4, 2, 0.4>, 12)
  sphere { <-0.5, 0.35, 0.1>, 0.35 Passive(0.75) transform { T } }
  box { <0.2, 0, -0.2>, <0.7, 0.5, 0.3> Passive(0.75) rotate y * 20 transform { T } }
  #break
#case (3)
  Panel(<-0.4, 1.98, -0.4>, <0.4, 2, 0.4>, 12)
  #break
#case (4)
  Panel(<-0.4, 1.98, -0.4>, <0.4, 2, 0.4>, 12)
  union {
    box { <-0.9, 0, 0.2>, <-0.1, 0.8, 0.3> }
    box { <-0.2, 0, -0.5>, <-0.1, 0.8, 0.3> }
    Passive(0.75) transform { T } }
  box { <0.3, 0, -0.3>, <0.8, 0.6, 0.2> Passive(0.75) rotate y * 35 transform { T } }
  #break
#case (5)
  Panel(<-1.45, 1.98, 1.0>, <-1.3, 2, 1.45>, 40)
  #break
#case (6)
  Panel(<-0.4, 1.9, -0.4>, <0.4, 1.92, 0.4>, 12)
  box { <-1.5, 0, -3>, <1.5, 2, 1.5> #if (Inverse) inverse #end Passive(0.75) transform { T } }
  #break
#end
