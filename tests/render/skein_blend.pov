// skein_blend.pov: expression_map against the items at its stops and the extrude whose radius blends theirs; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
// two blended pipelines a sample, on star sections whose corners split to the depth limit
#declare CheckRays = 700;
#declare CheckLattice = 0.6;
#include "skein_check.inc"

#declare RCircle = function(u) { 0.45 }
#declare RSquare = function(u) { 0.45*pow(pow(abs(cos(2*pi*u)), 6) + pow(abs(sin(2*pi*u)), 6), -1/6) }
#declare RStar = function(u) { 0.33 + 0.17*pow(abs(cos(5*pi*u)), 3) }
#declare Circle = expressions { extrude { radius 0.45 } }
#declare Square = expressions { extrude { radius function(u) { RSquare(u) } } }

// circle held to 0.3, blended to the square at 0.5, held to 0.7, blended to the star (an inline step) at 0.9
#declare Blended = skein {
  expressions {
    expression_map {
      [0 Circle] [0.3 Circle] [0.5 Square] [0.7 Square] [0.9 extrude { radius function(u) { RStar(u) } }] [1 extrude { radius function(u) { RStar(u) } }]
    }
    scale <1, 3, 1>
  }
  closed u  ends flat
}
#declare Weight = function(v) { select(v - 0.3, 0, select(v - 0.5, (v - 0.3)/0.2, 1)) }
#declare Late = function(v) { select(v - 0.7, 0, select(v - 0.9, (v - 0.7)/0.2, 1)) }
#declare BlendRadius = function(u, v) {
  select(v - 0.7, (1 - Weight(v))*RCircle(u) + Weight(v)*RSquare(u), (1 - Late(v))*RSquare(u) + Late(v)*RStar(u))
}
#declare ByFunction = skein { expressions { extrude { radius function(u, v) { BlendRadius(u, v) } }  scale <1, 3, 1> } closed u  ends flat }

#macro RadiusAt(Object, Y, U)
  #local N = <0, 0, 0>;
  #local Direction = <cos(2*pi*U), 0, -sin(2*pi*U)>;
  #local P = trace(Object, <0, Y, 0> + 5*Direction, -Direction, N);
  vlength(<P.x, 0, P.z>)
#end
#macro Worst(Object, V, Exact)
  #local E = 0;
  #for (I, 0, 23)
    #local U = (I + 0.37)/24;
    #local E = max(E, abs(RadiusAt(Object, 3*V, U) - Exact(U, V)));
  #end
  E
#end
#declare AtStops = max(Worst(Blended, 0.3, BlendRadius), Worst(Blended, 0.5, BlendRadius), Worst(Blended, 0.7, BlendRadius), Worst(Blended, 0.9, BlendRadius));
#declare Between = max(Worst(Blended, 0.15, BlendRadius), Worst(Blended, 0.4, BlendRadius), Worst(Blended, 0.6, BlendRadius), Worst(Blended, 0.83, BlendRadius), Worst(Blended, 0.95, BlendRadius));
#debug concat("expression_map: radius at the stops 0.3, 0.5, 0.7, 0.9 off the items by at most ", str(AtStops*1e9, 0, 3), "e-9; between them off the blend by ", str(Between*1e9, 0, 3), "e-9\n")
#if ((AtStops > 1e-9) | (Between > 1e-9))
  #declare Failures = Failures + 1;
#end
PairCheck("circle, square and star blended, against one extrude of the blended radius", Blended, ByFunction, 1e-6, 1)
ClosedCheck("circle, square and star blended", Blended)

// equal stops step: the circle up to 0.5, the square after it; and a map driven by pos.y instead of v
#declare Stepped = skein { expressions { scale <1, 3, 1>  expression_map { [0.5 Circle] [0.5 Square] } } closed u }
#declare CircleOnly = function(u, v) { 0.45 }
#declare StepCircle = Worst(Stepped, 0.49, CircleOnly);
#declare SquareOnly = function(u, v) { RSquare(u) }
#declare StepSquare = Worst(Stepped, 0.51, SquareOnly);
#debug concat("expression_map with equal stops: circle below off by ", str(StepCircle*1e9, 0, 3), "e-9, square above off by ", str(StepSquare*1e9, 0, 3), "e-9\n")
#if ((StepCircle > 1e-9) | (StepSquare > 1e-9))
  #declare Failures = Failures + 1;
#end
#declare ByHeight = skein {
  expressions {
    scale <1, 3, 1>
    expression_map { pos.y  [0.9 Circle] [1.5 Square] [2.1 Square] [2.7 extrude { radius function(u) { RStar(u) } }] }
  }
  closed u  ends flat
}
#declare ByV = skein {
  expressions {
    scale <1, 3, 1>
    expression_map { [0.3 Circle] [0.5 Square] [0.7 Square] [0.9 extrude { radius function(u) { RStar(u) } }] }
  }
  closed u  ends flat
}
PairCheck("expression_map driven by pos.y against the same driven by v", ByHeight, ByV, 1e-7, 1)

// a function axis inside an entry, in a skein with no function values, against the same driven by a function
#declare Plain = skein { expressions { translate <-0.5, 0, 0>  expression_map { uv.v  [0 translate <0, 0, 0>] [1 bend { axis function(t) { 0 }, function(t) { t }, function(t) { 0 }  rotate { angle 30 } }] } } }
#declare Driven = skein { expressions { translate <-0.5, 0, 0>  expression_map { function(uv) { uv.v }  [0 translate <0, 0, 0>] [1 bend { axis function(t) { 0 }, function(t) { t }, function(t) { 0 }  rotate { angle 30 } }] } } }
PairCheck("function axis in a map entry, no function values, against a function driver", Plain, Driven, 1e-9, 0)

#if (Failures > 0)
  #error concat("skein_blend: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_blend: every check within tolerance\n"
