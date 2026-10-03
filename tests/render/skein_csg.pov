// skein_csg.pov: skeins in union, merge, intersection, difference, inverse, clipped_by, bounded_by and nested CSG, against the same primitive compositions; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "skein_check.inc"
#include "skein_csg.inc"

#for (N, 1, 9)
  #declare A = Csg(N, 0)
  #declare B = Csg(N, 1)
  #declare Name = concat("csg ", str(N, 0, 0), ", ", CsgNames[N]);
  PairCheck(Name, A, B, ((N = 3) | (N = 5) ? 1e-7 : 1e-5), 1)
  InsideCheck(Name, A, B, 15)
  #if (N != 1) BoundaryCheck(Name, A, 300) #end
#end
#for (N, 10, 13)
  #declare A = Csg(N, 0)
  #declare Name = concat("csg ", str(N, 0, 0), ", ", CsgNames[N]);
  ClosedCheck(Name, A)
  #if (N != 1) BoundaryCheck(Name, A, 300) #end
#end

#if (Failures > 0)
  #error concat("skein_csg: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_csg: every check within tolerance\n"
