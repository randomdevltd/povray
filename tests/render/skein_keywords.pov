// skein_keywords.pov: `range` and `repeat` as map keywords beside the #range directive, in taken and skipped #switch, #if and #while branches; `closed` as a path word beside `closed` as the topology.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Count = 0;
#declare Case = 2;
#switch (Case)
  #range (0, 1)
    #declare Skipped = skein { expressions { extrude { radius map { uv.v range { from 0, 1  to 1, 2  repeat } } } } closed u }
    #declare Count = Count + 100;
  #break
  #range (2, 3)
    #declare Taken = skein { expressions { extrude { radius map { uv.v range { from 0, 1  to 1, 2  repeat } } } } closed u }
    #declare Count = Count + 1;
  #break
  #else
    #declare Count = Count + 1000;
#end
#if (0)
  #declare Skipped = skein { expressions { extrude { radius map { uv.v range { repeat } } } } closed u }
#else
  #declare Repeated = skein { expressions { extrude { radius map { uv.v linear { scale 2 } range { repeat } linear { offset 1 } } } } closed u }
  #declare Count = Count + 1;
#end
#declare I = 0;
#while (I < 2)
  #declare Mirrored = skein { expressions { extrude { radius map { uv.v linear { scale 2 } range { mirror } linear { offset 1 } } } } closed u }
  #declare I = I + 1;
#end
#declare Wrapped = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius map { uv.u linear { scale 3 } range { repeat } sin { amplitude 0.05 } linear { offset 0.5 } } }
  }
  closed u  ends flat
}
#declare Count = Count + I;

#if (Count != 4)
  #error concat("skein_keywords: expected 4 branches, counted ", str(Count, 0, 0), "\n")
#end
#macro Radius(Object, Height, Angle)
  #local N = <0, 0, 0>;
  #local Direction = <cos(radians(Angle)), 0, -sin(radians(Angle))>;
  #local P = trace(Object, <0, Height, 0> + 5 * Direction, -Direction, N);
  vlength(<P.x, 0, P.z>)
#end
#if ((abs(Radius(Taken, 0.25, 0) - 1.25) > 1e-6) | (abs(Radius(Wrapped, 1, 0) - 0.5) > 1e-6) | (abs(Radius(Wrapped, 1, 30) - 0.55) > 1e-6) |
     (abs(Radius(Repeated, 0.75, 0) - 1.5) > 1e-6) | (abs(Radius(Mirrored, 0.75, 0) - 1.5) > 1e-6))
  #error "skein_keywords: a map using range, repeat or mirror built the wrong shape\n"
#end

// `closed` names a path's own closure and the skein's topology, in different blocks of one skein
#declare ClosedPath = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius path { closed 0.4, 0.5, 0.45 } }
  }
  closed u  ends flat
}
#if ((abs(Radius(ClosedPath, 1/3, 0) - 0.45) > 1e-6) | (abs(Radius(ClosedPath, 1, 0) - 0.475) > 1e-6) |
     (abs(Radius(ClosedPath, 5/3, 0) - 0.425) > 1e-6))
  #error "skein_keywords: closed in a path beside closed as the topology built the wrong shape\n"
#end
#debug "skein_keywords: range and repeat parse as map keywords beside #range, and closed in both its places\n"
