// skein_values.pov: value expressions (sum and its siblings) against the custom functions they equal; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "skein_check.inc"

// Radius along a ray from the axis at height Y and angle Angle (the extrude places u at 2*pi*u, toward -z).
#macro RadiusAt(Object, Y, U)
  #local N = <0, 0, 0>;
  #local Direction = <cos(2*pi*U), 0, -sin(2*pi*U)>;
  #local P = trace(Object, <0, Y, 0> + 5*Direction, -Direction, N);
  vlength(<P.x, 0, P.z>)
#end
#macro Worst(Object, Exact)
  #local E = 0;
  #for (J, 1, 9)
    #for (I, 0, 11)
      #local U = (I + 0.37)/12;
      #local V = J/10;
      #local E = max(E, abs(RadiusAt(Object, 2*V, U) - Exact(U, V)));
    #end
  #end
  E
#end
#macro Report(Name, E, Limit)
  #debug concat(Name, ": off by at most ", str(E*1e9, 0, 3), "e-9\n")
  #if (E > Limit) #declare Failures = Failures + 1; #end
#end

// sum of a constant, a sin map of u, a function of v and a scalar path equals one function computing the same sum
#declare SumExact = function(u, v) { 0.4 + 0.05*sin(2*pi*3*u) + 0.1*v + 0.05*v }
#declare SumTube = skein { expressions { scale <1, 2, 1>  extrude { radius sum { 0.4  map { uv.u  sin { frequency 3  amplitude 0.05 } }  function(v) { 0.1*v }  path { 0, 0.05 } } } } closed u  ends flat }
#declare OneTube = skein { expressions { scale <1, 2, 1>  extrude { radius function(u, v) { 0.4 + 0.05*sin(2*pi*3*u) + 0.1*v + 0.05*v } } } closed u  ends flat }
#declare SumError = Worst(SumTube, SumExact);
#declare OneError = Worst(OneTube, SumExact);
Report("sum of four entries, radius against the exact sum", SumError, 1e-9)
Report("one function, radius against the exact sum", OneError, 1e-9)

// siblings: product, min and max of a constant and a linear map of v, and a nested sum
#declare ProductExact = function(u, v) { 0.5*(1 + 0.5*v) }
#declare MinExact = function(u, v) { min(0.55, 0.4 + 0.3*v) }
#declare MaxExact = function(u, v) { max(0.55, 0.4 + 0.3*v) }
#declare NestedExact = function(u, v) { 0.3 + 0.1*v + 0.2*(1 - v) }
#declare ProductTube = skein { expressions { scale <1, 2, 1>  extrude { radius product { 0.5  map { uv.v  linear { scale 0.5  offset 1 } } } } } closed u  ends flat }
#declare MinTube = skein { expressions { scale <1, 2, 1>  extrude { radius min { 0.55  map { uv.v  linear { scale 0.3  offset 0.4 } } } } } closed u  ends flat }
#declare MaxTube = skein { expressions { scale <1, 2, 1>  extrude { radius max { 0.55  map { uv.v  linear { scale 0.3  offset 0.4 } } } } } closed u  ends flat }
#declare NestedTube = skein { expressions { scale <1, 2, 1>  extrude { radius sum { 0.3  sum { function(v) { 0.1*v }  map { uv.v  range { to 0.2, 0 } } } } } } closed u  ends flat }
Report("product", Worst(ProductTube, ProductExact), 1e-9)
Report("min", Worst(MinTube, MinExact), 1e-9)
Report("max", Worst(MaxTube, MaxExact), 1e-9)
Report("nested sum", Worst(NestedTube, NestedExact), 1e-9)

// min() and max() as float functions, and a float named like a keyword, still parse as floats inside a skein value
#declare product = 0.1;
#declare FloatForms = skein { expressions { scale <1, 2, 1>  extrude { radius min(0.1, 0.2) + max(0, 0.1) + product } } closed u  ends flat }
#declare FloatExact = function(u, v) { 0.3 }
Report("min() and max() as float functions, a float named product", Worst(FloatForms, FloatExact), 1e-9)

// a displacement by a sum equals the same displacement by one function, and a translate by a sum the same translate
#declare DisplacedBySum = skein {
  expressions {
    extrude { radius 0.5 }
    extrude { axis x  radius 2 }
    displace sum { map { uv.u  sin { frequency 4  amplitude 0.03 } }  map { uv.v  cos { frequency 3  amplitude 0.02 } }  function(pos) { 0.01*pos.z } }
  }
  closed uv
}
#declare DisplacedByOne = skein {
  expressions {
    extrude { radius 0.5 }
    extrude { axis x  radius 2 }
    displace function(u, v, z) { 0.03*sin(2*pi*4*u) + 0.02*cos(2*pi*3*v) + 0.01*z }
  }
  closed uv
}
PairCheck("torus displaced by a sum against one function", DisplacedBySum, DisplacedByOne, 1e-8, 1)
#declare SwayBySum = skein {
  expressions { scale <1, 2, 1>  extrude { radius 0.5 }  translate { x sum { map { uv.v  sin { amplitude 0.2 } }  0.1 } } }
  closed u  ends flat
}
#declare SwayByOne = skein {
  expressions { scale <1, 2, 1>  extrude { radius 0.5 }  translate { x function(v) { 0.2*sin(2*pi*v) + 0.1 } } }
  closed u  ends flat
}
PairCheck("tube translated by a sum against one function", SwayBySum, SwayByOne, 1e-8, 1)

// noise and fbm: POV's own lattice noise (bozo, f_noise_generator with generator 2), centred on zero
#include "functions.inc"
#declare Noise = function(x, y, z) { 2*f_noise_generator(x, y, z, 2) - 1 }
#declare NoiseExact = function(u, v) { 0.5 + 0.05*Noise(1.5*cos(2*pi*u), 6*v, -1.5*sin(2*pi*u)) }
#declare FbmExact = function(u, v) {
  0.5 + 0.04*Noise(1.5*cos(2*pi*u), 6*v, -1.5*sin(2*pi*u)) + 0.02*Noise(3*cos(2*pi*u), 12*v, -3*sin(2*pi*u)) + 0.01*Noise(6*cos(2*pi*u), 24*v, -6*sin(2*pi*u))
}
#declare NoiseTube = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace map { pos  noise { frequency 3  amplitude 0.05 } } } closed u }
#declare NoiseFunctionTube = skein {
  expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace function(pos) { 0.05*Noise(3*pos.x, 3*pos.y, 3*pos.z) } }
  closed u
}
#declare FbmTube = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace map { pos  fbm { octaves 3  frequency 3  amplitude 0.04 } } } closed u }
#declare OctavesTube = skein {
  expressions {
    scale <1, 2, 1>  extrude { radius 0.5 }
    displace sum { map { pos  noise { frequency 3  amplitude 0.04 } }  map { pos  noise { frequency 6  amplitude 0.02 } }  map { pos  noise { frequency 12  amplitude 0.01 } } }
  }
  closed u
}
Report("noise map, radius against f_noise_generator", Worst(NoiseTube, NoiseExact), 1e-9)
Report("fbm map, radius against three f_noise_generator octaves", Worst(FbmTube, FbmExact), 1e-9)
Report("sum of three noise maps, radius against the same octaves", Worst(OctavesTube, FbmExact), 1e-9)
PairCheck("tube displaced by a noise map against a function of f_noise_generator", NoiseTube, NoiseFunctionTube, 1e-6, 0)

// A sphere from an extrude and a bend's shift with pole ends, displaced by a sum of noise octaves, stays a closed solid.
#declare NoisySphere = skein {
  expressions {
    extrude { radius map { uv.v  sin { frequency 0.5 } } }
    bend { axis y  translate sum { map { uv.v  cos { frequency 0.5  amplitude -1 } }  map { uv.v  linear { scale -1 } } } }
    displace sum { map { pos  noise { frequency 2  amplitude 0.08 } }  map { pos  noise { frequency 4  amplitude 0.04 } }  map { pos  noise { frequency 8  amplitude 0.02 } } }
  }
  closed u  ends pole
}
ClosedCheck("sphere displaced by three noise octaves", NoisySphere)

// arc as a value: an arc widening with v makes a fan, which the full arc resampled over u matches; thickened it is still a closed solid
#declare FanArc = function(v) { 90 + 180*v }
#declare Fan = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5  arc function(v) { FanArc(v) } } } }
#declare FanSampled = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
    sample { at <function(u, v) { u*FanArc(v)/360 }, function(v) { v }> }
  }
}
PairCheck("a fan from an arc varying over v against the full arc resampled over u", Fan, FanSampled, 1e-7, 0)

// outward from the axis a ray meets the fan at its radius within the arc, and nothing past the arc's end
#macro FanHit(Object, Y, Degrees)
  #local N = <0, 0, 0>;
  #local D = <cos(radians(Degrees)), 0, -sin(radians(Degrees))>;
  #local P = trace(Object, <0, Y, 0>, D, N);
  (vlength(N) = 0 ? 0 : vlength(<P.x, 0, P.z>))
#end
#declare FanOff = 0;
#declare FanPast = 0;
#for (J, 1, 9)
  #declare V = J/10;
  #declare FanOff = max(FanOff, max(abs(FanHit(Fan, 2*V, 0.5*FanArc(V)) - 0.5), abs(FanHit(Fan, 2*V, 0.98*FanArc(V)) - 0.5)));
  #declare FanPast = FanPast + (FanHit(Fan, 2*V, 1.02*FanArc(V)) > 0) + (FanHit(Fan, 2*V, FanArc(V) + 10) > 0);
#end
#debug concat("arc varying over v: the fan stands at radius 0.5 within ", str(FanOff*1e9, 0, 3), "e-9 inside its arc, with ",
              str(FanPast, 0, 0), " of 18 rays hitting past its end\n")
#if ((FanOff > 1e-9) | (FanPast > 0))
  #declare Failures = Failures + 1;
#end

#declare FanSolid = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5  arc function(v) { FanArc(v) } }
    envelope { thickness function(uv) { 0.06*(1 - pow(2*uv.v - 1, 8)) }  edge round }
  }
  closed u  ends sealed
}
ClosedCheck("a fan from a varying arc, thickened into a solid", FanSolid)

// image: a sheet displaced by a greyscale image lies on image_pattern's heights (bilinear and bicubic), with slopes matching its hits
#macro SlopeAgreement(Object, Count, Limit)
  #local Stream = seed(5);
  #local Good = 0;
  #local H = 1e-5;
  #for (I, 1, Count)
    #local X = 0.05 + 0.9*rand(Stream);
    #local Y = 0.05 + 0.9*rand(Stream);
    #local N = <0, 0, 0>;
    #local P = trace(Object, <X, Y, 1>, -z, N);
    #local PX = trace(Object, <X + H, Y, 1>, -z) - trace(Object, <X - H, Y, 1>, -z);
    #local PY = trace(Object, <X, Y + H, 1>, -z) - trace(Object, <X, Y - H, 1>, -z);
    #local F = vnormalize(vcross(PX, PY));
    #local Good = Good + (min(vlength(F - vnormalize(N)), vlength(F + vnormalize(N))) < Limit);
  #end
  Good
#end
#macro HeightError(Object, Pattern)
  #local Stream = seed(3);
  #local Largest = 0;
  #local Hits = 0;
  #for (I, 1, 2000)
    #local N = <0, 0, 0>;
    #local Q = trace(Object, <rand(Stream), rand(Stream), 1>, <0.2*(rand(Stream) - 0.5), 0.2*(rand(Stream) - 0.5), -1>, N);
    #if (vlength(N) > 0)
      #local Hits = Hits + 1;
      #local Height = Pattern(Q.x, Q.y, 0);
      #local Largest = max(Largest, abs(Q.z - 0.2*Height));
    #end
  #end
  (Hits < 1500 ? 1 : Largest)
#end
#declare Heights = function { pattern { image_pattern { png "Mount1.png" interpolate 2 } } }
#declare HeightsCubic = function { pattern { image_pattern { png "Mount1.png" interpolate 3 } } }
#declare ImageSheet = skein { expressions { displace map { uv  image { png "Mount1.png" }  linear { scale 0.2 } } } }
#declare CubicSheet = skein { expressions { displace map { uv  image { png "Mount1.png"  interpolate 3 }  linear { scale 0.2 } } } }
#declare BilinearError = HeightError(ImageSheet, Heights);
#declare BicubicError = HeightError(CubicSheet, HeightsCubic);
Report("image height map, bilinear: hits on z = 0.2 image_pattern(x, y)", BilinearError, 1e-6)
Report("image height map, bicubic: hits on z = 0.2 image_pattern(x, y)", BicubicError, 1e-6)
#declare ImageSlopes = SlopeAgreement(ImageSheet, 1000, 1e-3);
#declare CubicSlopes = SlopeAgreement(CubicSheet, 1000, 1e-3);
#debug concat("image: normals match differences of hits at ", str(ImageSlopes, 0, 0), " of 1000 points (bilinear, creased at texel edges), ",
              str(CubicSlopes, 0, 0), " (bicubic)\n")
#if ((ImageSlopes < 950) | (CubicSlopes < 990))
  #declare Failures = Failures + 1;
#end

// cells: 2D distances to POV's crackle nuclei; no custom function computes them, so check range and slopes
#declare CellSheet = skein { expressions { displace map { uv  cells { frequency 5  amplitude 0.05 } } } }
#declare CellHigh = 0;
#declare CellLow = 1;
#declare CellStream = seed(9);
#for (I, 1, 2000)
  #declare N = <0, 0, 0>;
  #declare P = trace(CellSheet, <rand(CellStream), rand(CellStream), 1>, -z, N);
  #declare CellHigh = max(CellHigh, P.z);
  #declare CellLow = min(CellLow, P.z);
#end
#declare CellSlopes = SlopeAgreement(CellSheet, 1000, 1e-4);
#debug concat("cells: f1 heights from ", str(CellLow, 0, 6), " to ", str(CellHigh, 0, 6), " (0 to 0.05 sqrt 2); normals match differences of hits at ",
              str(CellSlopes, 0, 0), " of 1000 points\n")
#if ((CellLow < 0) | (CellHigh > 0.05*sqrt(2)) | (CellSlopes < 980))
  #declare Failures = Failures + 1;
#end

// declared functions as values (radius TaperRadius, displace SmallBumps) against the same inline; a call still reads as a float
#include "skein_shapes.inc"
PairCheck("declared radius and displacement against the same inline", SkeinDeclared, SkeinInline, 1e-12, 0)
#declare CallForm = skein { expressions { scale <1, 2, 1>  extrude { radius TaperRadius(0.25) + 0.1 } } closed u  ends flat }
#declare CallExact = function(u, v) { 0.55 }
Report("radius TaperRadius(0.25) + 0.1 as a float expression", Worst(CallForm, CallExact), 1e-9)

#if (Failures > 0)
  #error concat("skein_values: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_values: every check within tolerance\n"
