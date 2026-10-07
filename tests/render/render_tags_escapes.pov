#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (UseSDL) #declare UseSDL = 1; #end
global_settings { assumed_gamma 1 }
camera {
  orthographic location -5*z look_at 0 right 2*x up 2*y
  #if (!Reference & UseSDL) filter_tags { "O'Brien" & "quote\"tag" & "path\\tag" | none } #end
}
sphere {
  0, 0.8 pigment { rgb <0.5, 0.25, 1> } finish { emission 1 diffuse 0 }
  tags { "O'Brien", "quote\"tag", "path\\tag" }
}
#if (!Reference)
  sphere { -2*z, 1 pigment { rgb 1 } finish { emission 1 diffuse 0 } tags { "discard" } }
#end
