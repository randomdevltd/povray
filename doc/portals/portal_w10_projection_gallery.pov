// Eight live panoramic photographs of one hidden scene, including two arbitrary function cameras.
#version 4.0;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"
#include "projections.inc"

camera { location <0, 2.8, -8.5> look_at <0, 2.25, 0> angle 44 }
light_source { <-5, 8, -6> rgb 1.2 }
box { <-7, 0, 0.2>, <7, 5.4, 0.45> pigment { rgb <0.16, 0.18, 0.23> } }
box { <-7, -0.12, -4>, <7, 0, 2> pigment { rgb <0.32, 0.25, 0.2> } }

#declare P = <500, 1.2, 0>;
light_group {
  light_source { P + <-20, 30, -15> rgb 1.4 }
  sphere { P, 100 inverse pigment { rgb <0.22, 0.42, 0.72> } finish { Glow } no_shadow }
  box { P + <-18, -1.25, -18>, P + <18, -1.2, 18> pigment { checker rgb 0.15 rgb 0.75 scale 1.2 } }
  #for (I, 0, 11)
    #local A = radians(I * 30);
    #local Q = P + <sin(A) * 8, 0, cos(A) * 8>;
    cylinder { Q - 1.2 * y, Q + 4 * y, 0.055 pigment { rgb <1, 0.2 + 0.05 * I, 0.1> } }
    sphere { Q, 0.7 + 0.08 * mod(I, 3) pigment { rgb <0.15 + 0.07 * I, 0.8 - 0.04 * I, 1 - 0.06 * I> } }
  #end
  torus { 5, 0.18 rotate 90 * x translate P pigment { rgb <1, 0.75, 0.15> } }
  global_lights off
}

#declare Projection0 = CylindricalProjectionCamera(ProjectionEquirectangular, -58, 72, ProjectionSingleOrigin, 0, 0, P, z, x, y);
#declare Projection1 = CylindricalProjectionCamera(ProjectionMercator, -58, 72, ProjectionSingleOrigin, 0, 0, P, z, x, y);
#declare Projection2 = CylindricalProjectionCamera(ProjectionMiller, -58, 72, ProjectionSingleOrigin, 0, 0, P, z, x, y);
#declare Projection3 = CylindricalProjectionCamera(ProjectionStereographic, -58, 72, ProjectionSingleOrigin, 0, 0, P, z, x, y);
#declare Projection4 = CylindricalProjectionCamera(ProjectionEqualArea, -58, 72, ProjectionSingleOrigin, 0, 0, P, z, x, y);
#declare Projection5 = CylindricalProjectionCamera(ProjectionMercator, -35, 40, ProjectionCylinderOrigin, 4, 0, P, z, x, y);
#declare TwistY0 = asinh(tan(radians(-58)));
#declare TwistY1 = asinh(tan(radians(72)));
#declare TwistLat = function(v) { atan(sinh(TwistY0 + (v + 0.5) * (TwistY1 - TwistY0))) };
#declare TwistLon = function(u, v) { 2 * pi * u + 0.7 * sin(2 * pi * v) };
#declare Projection6 = camera {
  user_defined location P
  direction {
    function { sin(TwistLon(x, y)) * cos(TwistLat(y)) }
    function { sin(TwistLat(y)) }
    function { cos(TwistLon(x, y)) * cos(TwistLat(y)) }
  }
};
#declare TurnLon = function(u) { 2 * pi * u };
#declare TurnX = P.x;
#declare TurnY = P.y;
#declare TurnZ = P.z;
#declare Projection7 = camera {
  user_defined
  location {
    function { TurnX + 12 * sin(TurnLon(x)) }
    function { TurnY }
    function { TurnZ + 12 * cos(TurnLon(x)) }
  }
  direction {
    function { -sin(TurnLon(x)) }
    function { 2.2 * y }
    function { -cos(TurnLon(x)) }
  }
};
#declare Cams = array[8] {
  camera { Projection0 }, camera { Projection1 }, camera { Projection2 },
  camera { Projection3 }, camera { Projection4 }, camera { Projection5 },
  camera { Projection6 }, camera { Projection7 }
};
#declare Frames = array[8] { <0.22, 0.45, 0.9>, <0.9, 0.32, 0.16>, <0.2, 0.72, 0.4>, <0.72, 0.3, 0.82>, <0.85, 0.68, 0.12>, <0.1, 0.72, 0.72>, <0.95, 0.35, 0.62>, <0.35, 0.85, 0.9> };
#for (I, 0, 7)
  #local Place = <-2.7 + 1.8 * mod(I, 4), 0.65 + 2.15 * (1 - floor(I / 4)), 0>;
  object {
    Panel(1.65, 1.075, pigment { screen { camera { Cams[I] } } scale <1.65, 1.075, 1> }, Frames[I])
    translate Place
  }
#end
