// skein_crease.pov: rays from outside a cracked log first meet it where they enter, also rays that clip a crack's rim within a slope step of its crease.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Radius = function(v) { 0.6 - 0.56*v }
#declare Cracks = function { pattern { crackle } }
#declare Crack = function(a, b, x, y, z) {
  -0.07*Radius(b)*min(1, 20*b, 20*(1 - b))*max(0, 1 - Cracks(5*Radius(b)*cos(2*pi*a), 5*Radius(b)*sin(2*pi*a), 8*b)/0.12)
}
#declare Log = skein {
  expressions {
    scale <1, 4, 1>
    extrude { radius function(v) { Radius(v) } }
    displace function(uv, pos) { Crack(uv.u, uv.v, pos.x, pos.y, pos.z) }
  }
  closed u  ends flat
}

#declare Failures = 0;
#macro Shoot(Origin, Direction)
  #local N = <0, 0, 0>;
  #local P = trace(Log, Origin, Direction, N);
  #if (vlength(N) = 0)
    #debug concat("  no hit from <", vstr(Origin, ", ", 0, 6), "> along <", vstr(Direction, ", ", 0, 9), ">\n")
    #declare Failures = Failures + 1;
  #elseif (vdot(N, Direction) >= 0)
    #debug concat("  first hit is an exit at <", vstr(P, ", ", 0, 6), "> from <", vstr(Origin, ", ", 0, 6), "> along <", vstr(Direction, ", ", 0, 9), ">\n")
    #declare Failures = Failures + 1;
  #end
#end

#declare Rim = array[11] {
  <-0.96163796849320471, -0.24585549110594071, -0.12168605115344538>,
  <-0.97217269610349766, -0.19164203012856371, -0.13473522642226526>,
  <-0.98265616331539252, -0.12942103666002899, -0.13280459317401663>,
  <-0.99165732560082265, 0.0008536375003133491, -0.12889926254731726>,
  <-0.98648368733817726, -0.12483003830180106, -0.10614799175323215>,
  <-0.98769809476998227, -0.1317855444538728, -0.084172702586667106>,
  <-0.9794571950529245, -0.19307800562214919, -0.058176342305363685>,
  <-0.99456341171878393, -0.072771824737250565, 0.074484102966362417>,
  <-0.99168912230887418, 0.099878789968406739, 0.081098162794860235>,
  <-0.94638101880436065, -0.29735191968913693, 0.12627273301867462>,
  <-0.97095489782312094, -0.1797005381363985, 0.15796931026874106>
}
#for (I, 0, 10)
  Shoot(<3.4, 1, -1.4695761589768238e-16>, Rim[I])
#end

#declare Stream = seed(1616);
#declare Rays = 400;
#declare Lo = min_extent(Log);
#declare Hi = max_extent(Log);
#for (I, 1, Rays)
  #local Origin = (Lo + Hi)/2 + vlength(Hi - Lo)*vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
  #local Target = <0, Lo.y + (Hi.y - Lo.y)*rand(Stream), 0>;
  Shoot(Origin, Target - Origin)
#end

#if (Failures > 0)
  #error concat("skein_crease: ", str(Failures, 0, 0), " of ", str(Rays + 11, 0, 0), " rays from outside do not first enter the log\n")
#end
#debug concat("skein_crease: all ", str(Rays + 11, 0, 0), " rays from outside first enter the log\n")
