// skein_bend.pov: bend about a straight axis is the rotate, translate and scale steps; about a curved one it moves material about the frame at each station, and along follows a path.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 900;
#declare CheckLattice = 0.6;
#include "skein_check.inc"

#macro YPath() path { -0.5, <0, -0.5, 0>, 2.5, <0, 2.5, 0> } #end
#macro BowPath() path { -0.5, <0, -0.5, 0>, 0.8, <0.3, 0.8, 0.12> handle <0.12, 0.5, 0.1>, 2.5, <0, 2.5, 0.5> } #end
#macro CurlPath() path { -1, <-0.8, -1, 0.3>, 0, <0.2, 0, -0.15> handle <0.3, 0.5, 0.2>, 1, <0.9, 1, 0.5> } #end

#declare Tube = expressions { scale <1, 1.6, 1>  extrude { radius 0.4 } }
#declare Oval = expressions { Tube  scale <1.5, 1, 1> }
#declare Skew = vnormalize(<1, 2, 0.5>);
#declare Round = skein { expressions { Tube } closed u  ends flat }
#declare Plain = skein { expressions { Oval } closed u  ends flat }

// 1: a turn about a straight axis is the rotate step, constant or varying, on any axis
#declare TurnA = skein { expressions { Oval  rotate { axis y  angle 37 } } closed u  ends flat }
#declare TurnB = skein { expressions { Oval  bend { axis y  rotate { angle 37 } } } closed u  ends flat }
PairCheck("bend rotate by a constant against rotate", TurnB, TurnA, 1e-7, 1)

#declare TwistA = skein { expressions { Oval  rotate { axis y  angle function(v) { 140*v } } } closed u  ends flat }
#declare TwistB = skein { expressions { Oval  bend { axis y  rotate { angle function(v) { 140*v } } } } closed u  ends flat }
PairCheck("bend rotate by a function of v against rotate", TwistB, TwistA, 1e-7, 1)

#declare SkewA = skein { expressions { Oval  rotate { axis Skew  angle 50 } } closed u  ends flat }
#declare SkewB = skein { expressions { Oval  bend { axis Skew  rotate { angle 50 } } } closed u  ends flat }
PairCheck("bend rotate about a skew axis against rotate", SkewB, SkewA, 1e-7, 1)

// 2: a shift along a straight axis is the translate step
#declare LiftA = skein { expressions { Oval  translate { y function(v) { 0.5*v } } } closed u  ends flat }
#declare LiftB = skein { expressions { Oval  bend { axis y  translate function(v) { 0.5*v } } } closed u  ends flat }
PairCheck("bend translate by a function of v against translate", LiftB, LiftA, 1e-7, 1)

#declare StepA = skein { expressions { Oval  translate 0.4*Skew } closed u  ends flat }
#declare StepB = skein { expressions { Oval  bend { axis Skew  translate 0.4 } } closed u  ends flat }
PairCheck("bend translate along a skew axis against translate", StepB, StepA, 1e-7, 1)

// 3: the coil's climb as a bend step, against the same spring shifted by a plain translate
#declare CoilShift = skein {
  expressions {
    extrude { radius 0.08 }
    extrude { axis x  radius 0.6  arc 6*360 }
    translate { x map { uv.v  linear { scale 0.6*pi } } }
  }
  closed u  ends flat
}
#declare CoilBend = skein {
  expressions {
    extrude { radius 0.08 }
    extrude { axis x  radius 0.6  arc 6*360 }
    bend { axis x  translate map { uv.v  linear { scale 0.6*pi } } }
  }
  closed u  ends flat
}
PairCheck("the coil's climb as a bend translate", CoilBend, CoilShift, 1e-7, 1)

// the axis may also be three functions of t, or a path given on the surface
#declare FnTwist = skein { expressions { Oval  bend { axis function(t) { 0 }, function(t) { t }, function(t) { 0 }  rotate { angle function(v) { 140*v } } } } closed u  ends flat }
PairCheck("bend rotate about a function axis against rotate", FnTwist, TwistA, 1e-7, 1)

#declare OnSurface = skein { expressions { Tube  bend { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  rotate { angle 30 } } } closed u  ends flat }
#declare InSpace = skein { expressions { Tube  bend { axis path { <-0.4, 0, 0>, <-0.4, 1.6, 0> }  rotate { angle 30 } } } closed u  ends flat }
PairCheck("a sample_path bend axis against the same axis written absolutely", OnSurface, InSpace, 1e-7, 1)

// 4: a path axis along y places its frame on the y axis, so the turn there is the rotate step again
#declare PathTwist = skein { expressions { Oval  bend { axis YPath()  rotate { angle function(v) { 140*v } } } } closed u  ends flat }
PairCheck("bend rotate about a path axis against rotate", PathTwist, TwistA, 1e-7, 1)

#declare RoundTurned = skein { expressions { Tube  bend { axis YPath()  rotate { angle 48 } } } closed u  ends flat }
PairCheck("a constant turn about a path axis leaves a round tube alone", RoundTurned, Round, 1e-7, 1)

// a full turn about a curved axis is the identity, whatever its frame does
#declare Full = skein { expressions { Oval  bend { axis BowPath()  rotate { angle 360 } } } closed u  ends flat }
PairCheck("a full turn about a curved axis leaves the shape alone", Full, Plain, 1e-7, 1)

// a sheet of constant y has one station, so its turn is a rotation about the frame there: the path's point and tangent at t = 0
#declare Sheet = expressions { translate <-0.5, -0.5, 0>  scale <1.2, 0.9, 1>  rotate x*-90 }
#declare Station = <0.2, 0, -0.15>;
#declare Tangent = vnormalize(<0.3, 0.5, 0.2>);
#declare FlatA = skein { expressions { Sheet  bend { axis CurlPath()  rotate { angle 55 } } } }
#declare FlatB = skein { expressions { Sheet  rotate { axis Tangent  angle 55  about Station } } }
SheetCheck("a turn about a curved axis at one station is a rotation about its frame", FlatA, FlatB, 1e-7)

// 5: a scale about a straight axis is the scale step in x and z, the two directions out from it
#declare WideA = skein { expressions { Oval  scale { x 1.4  z 1.4 } } closed u  ends flat }
#declare WideB = skein { expressions { Oval  bend { axis y  scale 1.4 } } closed u  ends flat }
PairCheck("bend scale by a constant against scale", WideB, WideA, 1e-7, 1)

#declare TaperA = skein { expressions { Oval  scale { x function(v) { 1 - 0.4*v }  z function(v) { 1 - 0.4*v } } } closed u  ends flat }
#declare TaperB = skein { expressions { Oval  bend { axis y  scale function(v) { 1 - 0.4*v } } } closed u  ends flat }
PairCheck("bend scale by a function of v against scale", TaperB, TaperA, 1e-7, 1)

// both factors one is the identity, and must box as tightly as the shape it leaves alone
#declare OneA = skein { expressions { Oval  bend { axis y  scale 1 } } closed u  ends flat }
#declare OneB = skein { expressions { Oval  bend { axis BowPath()  scale 1 } } closed u  ends flat }
PairCheck("bend scale by one about a straight axis", OneA, Plain, 1e-7, 1)
PairCheck("bend scale by one about a curved axis", OneB, Plain, 1e-7, 1)
#declare StraightBox = vlength(min_extent(OneA) - min_extent(Plain)) + vlength(max_extent(OneA) - max_extent(Plain));
#declare CurvedBox = vlength(min_extent(OneB) - min_extent(Plain)) + vlength(max_extent(OneB) - max_extent(Plain));
#debug concat("bend scale by one: box differs from the plain tube by ", str(StraightBox, 0, 15), " straight, ", str(CurvedBox, 0, 15), " curved\n")
#if ((StraightBox > 1e-15) | (CurvedBox > 1e-9))
  #declare Failures = Failures + 1;
#end

// a path axis along y keeps its frame on the y axis, so a scale there is the scale step again
#declare PathTaper = skein { expressions { Oval  bend { axis YPath()  scale function(v) { 1 - 0.4*v } } } closed u  ends flat }
PairCheck("bend scale about a path axis against scale", PathTaper, TaperA, 1e-7, 1)

// about a curved axis, a sheet of constant y scales and turns about the frame at its one station: 0.7 out from the tangent, 1 along it
#declare Factor = 0.7;
#declare M = <Factor, Factor, Factor> + (1 - Factor) * <Tangent.x*Tangent.x, Tangent.y*Tangent.y, Tangent.z*Tangent.z>;
#declare Mxy = (1 - Factor) * Tangent.x * Tangent.y;
#declare Mxz = (1 - Factor) * Tangent.x * Tangent.z;
#declare Myz = (1 - Factor) * Tangent.y * Tangent.z;
#declare FlatScaleA = skein { expressions { Sheet  bend { axis CurlPath()  scale Factor  rotate { angle 55 } } } }
#declare FlatScaleB = skein {
  expressions {
    Sheet
    translate -Station
    matrix <M.x, Mxy, Mxz, Mxy, M.y, Myz, Mxz, Myz, M.z, 0, 0, 0>
    rotate { axis Tangent  angle 55 }
    translate Station
  }
}
SheetCheck("a scale and turn about a curved axis at one station against the same matrix", FlatScaleA, FlatScaleB, 1e-7)

// a twist about a curved axis moves the material and leaves the solid closed
#declare Twisted = skein { expressions { Oval  bend { axis BowPath()  rotate { angle function(v) { 120*v } } } } closed u  ends flat }
ClosedCheck("a twist about a curved axis", Twisted)

#declare Pinched = skein { expressions { Oval  bend { axis BowPath()  scale function(v) { 1 - 0.5*v } } } closed u  ends flat }
ClosedCheck("a taper about a curved axis", Pinched)

#declare Stream = seed(4321);
#declare Moved = 0;
#declare Lo = min_extent(Plain) - 0.1;
#declare Hi = max_extent(Plain) + 0.1;
#for (I, 1, 400)
  #declare Q = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
  #declare Moved = Moved + (inside(Twisted, Q) != inside(Plain, Q));
#end
#debug concat("a twist about a curved axis: the material moved at ", str(Moved, 0, 0), " of 400 probes\n")
#if (Moved = 0)
  #declare Failures = Failures + 1;
#end

// 6: along follows a path, carrying the cross section round: along the bend's own axis it is the identity
#declare Self = skein { expressions { Oval  bend { axis y  along y } } closed u  ends flat }
#declare Mirror = skein { expressions { Oval  bend { axis y  along x } } closed u  ends flat }
PairCheck("along the bend's own axis is the identity", Self, Plain, 1e-7, 1)
#declare SelfBox = vlength(min_extent(Self) - min_extent(Plain)) + vlength(max_extent(Self) - max_extent(Plain));
#declare MirrorBox = vlength(min_extent(Mirror) - <min_extent(Plain).y, min_extent(Plain).x, min_extent(Plain).z>) +
                     vlength(max_extent(Mirror) - <max_extent(Plain).y, max_extent(Plain).x, max_extent(Plain).z>);
#debug concat("along: box differs from the shape it leaves alone by ", str(SelfBox, 0, 15), ", and laid along x by ", str(MirrorBox, 0, 15), "\n")
#if ((SelfBox > 1e-15) | (MirrorBox > 1e-7))
  #declare Failures = Failures + 1;
#end

// a straight path is the same rigid motion, a quarter turn about the axis: a path's frame starts at z where a direction's starts at x
#declare Line = skein { expressions { Oval  bend { axis y  along path { <0, 0, 0>, <0, 1.6, 0> } } } closed u  ends flat }
#declare Quarter = skein { expressions { Oval  rotate y*-90 } closed u  ends flat }
PairCheck("along a straight path is a quarter turn about the axis", Line, Quarter, 1e-7, 1)
#declare LineBox = vlength(min_extent(Line) - min_extent(Quarter)) + vlength(max_extent(Line) - max_extent(Quarter));
#debug concat("along a straight path: box differs from the quarter turn by ", str(LineBox, 0, 15), "\n")
#if (LineBox > 1e-9)
  #declare Failures = Failures + 1;
#end

// a helix has constant speed, so material at y sits at the helix point at arc length y, and nothing reaches past the end
#declare HelixSpeed = sqrt(pow(2*pi*1.5*0.6, 2) + 16);
#macro HelixAt(S) <0.6*cos(2*pi*1.5*S/HelixSpeed), 4*S/HelixSpeed, 0.6*sin(2*pi*1.5*S/HelixSpeed)> #end
#declare Strand = skein {
  expressions {
    scale <1, 2.5, 1>
    extrude { radius 0.08 }
    bend { axis y  along function(t) { 0.6*cos(2*pi*1.5*t) }, function(t) { 4*t }, function(t) { 0.6*sin(2*pi*1.5*t) } }
  }
  closed u  ends flat
}
#declare ArcError = 0;
#declare Stream = seed(99);
#for (I, 0, 200)
  #declare S = 2.5*I/200;
  #declare C = HelixAt(S);
  #declare Across = vnormalize(vcross(HelixAt(S + 1e-5) - HelixAt(S - 1e-5), <rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>));
  #declare N = <0, 0, 0>;
  #declare P = trace(Strand, C, Across, N);
  #declare ArcError = max(ArcError, (vlength(N) = 0 ? 1 : abs(vlength(P - C) - 0.08)));
#end
#declare Past = 0;
#for (I, 1, 20)
  #declare Past = Past + inside(Strand, HelixAt(2.5 + 0.02 + 0.05*I));
#end
#debug concat("a tube 2.5 long bent along a helix: rays from the centre line at arc length y leave at radius 0.08 within ",
              str(ArcError * 1e9, 0, 3), "e-9, and ", str(Past, 0, 0), " of 20 points past its end are inside it\n")
#if ((ArcError > 1e-6) | (Past > 0))
  #declare Failures = Failures + 1;
#end

// material past an open path's ends runs on straight along the end tangent, hit by every ray and inside() there
#declare Long = expressions { scale <1, 3, 1>  translate <0, -1, 0>  extrude { radius 0.1 } }
#declare Runon = skein { expressions { Long  bend { axis y  along path { <0, 0, 0>, <1, 0, 0> } } } closed u  ends flat }
#declare Laid = skein { expressions { Long  rotate z*-90 } closed u  ends flat }
PairCheck("a tube 3 long along a path 1 long runs on straight past both ends", Runon, Laid, 1e-7, 1)

// a polyline's run-on follows its end segments: below 0 back along the first, past 0.5 + sqrt(1/2) on along the last
#declare Knee = skein { expressions { Long  bend { axis y  along path { <0, -0.5, 0>, <0.5, 0, 0>, <1, 0, 0> } } } closed u  ends flat }
#declare Corner = 0.5 + sqrt(0.5);
#declare RunError = 0;
#declare RunOut = 0;
#declare Stream = seed(7);
#for (I, 0, 100)
  #declare S = -0.98 + I * 2.96 / 100;
  #if ((S < -0.02) | (S > Corner + 0.02))
    #declare T = (S < 0 ? <1, 1, 0>/sqrt(2) : x);
    #declare C = (S < 0 ? <0, -0.5, 0> + S*T : <1 + S - Corner, 0, 0>);
    #declare Across = vnormalize(vcross(T, <rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>));
    #declare N = <0, 0, 0>;
    #declare P = trace(Knee, C, Across, N);
    #declare RunError = max(RunError, (vlength(N) = 0 ? 1 : abs(vlength(P - C) - 0.1)));
    #declare RunOut = RunOut + !inside(Knee, C);
  #end
#end
#debug concat("a tube run on past both ends of a polyline: rays from the centre line leave at radius 0.1 within ",
              str(RunError * 1e9, 0, 3), "e-9, and ", str(RunOut, 0, 0), " centre points are outside it\n")
#if ((RunError > 1e-6) | (RunOut > 0))
  #declare Failures = Failures + 1;
#end
ClosedCheck("a tube run on past both ends of a polyline", Knee)

#declare Bent = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.1 }
    bend { axis y  along path { <0, 0, 0> handle <0, 0.8, 0>, <1.2, 1.6, 0.4> handle <0.8, 0.4, 0.4>  arclength } }
  }
  closed u  ends flat
}
ClosedCheck("a tube bent along a path", Bent)

#if (Failures > 0)
  #error concat("skein_bend: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_bend: bend matches the steps it must, moves material about a curved axis, and follows a path along it\n"
