#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
#ifdef (Filter_Tags) #error "Command-line filter leaked into SDL" #end
#declare Filter_Tags = 17;
#declare BuildCount = 0;
global_settings { assumed_gamma 1 }
camera { orthographic location -5*z look_at 0 right 2*x up 2*y }
#macro MakeSphere(Tag, Place)
  #declare BuildCount = BuildCount + 1;
  #if (!Reference | strcmp(Tag, "keep") = 0)
    sphere {
      Place, 0.8
      pigment { rgb #if (strcmp(Tag, "keep") = 0) <1, 0.25, 0> #else <0, 0, 1> #end }
      finish { emission 1 diffuse 0 } tags { Tag }
    }
  #end
#end
MakeSphere("keep", 0)
MakeSphere("discard", -2*z)
#if (BuildCount != 2 | Filter_Tags != 17) #error "Filtering affected macro execution or SDL state" #end
