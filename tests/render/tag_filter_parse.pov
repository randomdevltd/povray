#version 4.0;
// Filter_Tags acts after the parse: tagged, trace(), inside() and extents see what an unfiltered parse sees, written to
// tag_filter_parse.txt, while the render leaves the filtered objects out. Reference=1 places only what !"hidden*" keeps.
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1 }
background { rgb 0 }
camera { orthographic location <0, 1, -10> look_at <0, 1, 0> right 8*x up 4*y tags { "view" } }
#declare Glow = finish { emission 1 diffuse 0 }
plane { y, -1 pigment { rgb 0.5 } finish { ambient 0 emission 0 diffuse 0.8 } tags { "floor" } }
light_source { <0, 6, -6> rgb 0.3 tags { "key" } }
box { <-3.5, -0.5, -0.5>, <-2.5, 0.5, 0.5> pigment { rgb <1, 0.3, 0.2> } finish { Glow } tags { "keep" } }
#if (!Reference)
  sphere { <3, 0, 0>, 0.6 pigment { rgb <0.2, 0.4, 1> } finish { Glow } tags { "hidden" } }
#end
// a union cut by the filter: its kept half stays, its hidden half goes
union {
  box { <-1.5, -0.5, -0.5>, <-0.5, 0.5, 0.5> pigment { rgb <0.3, 1, 0.3> } tags { "keep", inherit } }
  #if (!Reference) box { <-0.25, -0.5, -0.5>, <0.75, 0.5, 0.5> pigment { rgb 1 } tags { "hidden", inherit } } #end
  finish { Glow }
  tags { "group" }
}
// a light the filter takes out of a union it keeps
union {
  box { <3.2, 1.2, -0.3>, <3.6, 1.6, 0.3> pigment { rgb 0.7 } }
  #if (!Reference) light_source { <0, 0.8, -2> rgb <1, 0, 1> tags { "hidden-light" } } #end
  tags { "rig" }
}
// a surface light whose container loses a part: it emits from what is left
union {
  box { <1.2, 1.5, -0.5>, <2.2, 1.7, 0.5> }
  #if (!Reference) box { <-2.2, 1.5, -0.5>, <-1.2, 1.7, 0.5> tags { "hidden" } } #end
  pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
  light { pigment { rgb <1, 0.8, 0.5> } brightness 2 samples 8 }
  tags { "lamp" }
}
#if (!Reference)
  #declare Hidden = tagged { "hidden" };
  #declare Group = tagged { "group" };
  #declare Normal = <0, 0, 0>;
  #fopen F "tag_filter_parse.txt" write
  #write (F, "hidden min ", vstr(3, min_extent(Hidden), " ", 0, 6), "\n")
  #write (F, "hidden max ", vstr(3, max_extent(Hidden), " ", 0, 6), "\n")
  #declare Hit = trace(Hidden, <3, 0, -5>, z, Normal);
  #write (F, "trace hidden ", vstr(3, Hit, " ", 0, 6), " normal ", vstr(3, Normal, " ", 0, 6), "\n")
  #declare Hit = trace(Group, <0.25, 0, -5>, z, Normal);
  #write (F, "trace group ", vstr(3, Hit, " ", 0, 6), " normal ", vstr(3, Normal, " ", 0, 6), "\n")
  #write (F, "inside hidden ", str(inside(Hidden, <3, 0, 0>), 0, 0), " ", str(inside(Hidden, <0.25, 0, 0>), 0, 0), "\n")
  #fclose F
  // the answers an unfiltered parse gives, so a filter that leaked into the parse fails here
  #if (vlength(trace(Hidden, <3, 0, -5>, z) - <3, 0, -0.6>) > 1e-6)
    #error "tagged { \"hidden\" } lost the hidden sphere during the parse"
  #end
  #if (vlength(trace(Group, <0.25, 0, -5>, z) - <0.25, 0, -0.5>) > 1e-6)
    #error "trace() missed the hidden part of the group during the parse"
  #end
  #if (!inside(Hidden, <3, 0, 0>))
    #error "inside() missed the hidden sphere during the parse"
  #end
#end
