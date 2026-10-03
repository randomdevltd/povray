// skein_bark.inc: bark cracks from a 3D crackle on tapered skeins, the sample point chosen by Method; set Shape and Method, then include again to switch.
#ifndef (Shape) #declare Shape = 1; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Twist) #declare Twist = 240; #end
#ifndef (TwistInFold) #declare TwistInFold = 0; #end
#ifndef (BarkG) #declare BarkG = 2.5; #end

#declare BarkL = 4;
#declare BarkK = 5;
#declare BarkDepth = 0.07;
#declare BarkWidth = 0.12;
#declare BarkBend = 45;
#ifdef (BarkR) #undef BarkR #undef BarkRd #undef BarkSpeed #undef BarkS #undef BarkTX #undef BarkTY #undef BarkTZ #undef BarkCracks #undef BarkPattern #undef BarkCrack #end

#if (Shape = 1)
  #declare BarkR = function(a) { 0.6 - 0.56*a }
  #declare BarkRd = function(a) { -0.56 }
  #declare BarkRref = 0.32;
#else
  #declare BarkR = function(a) { 0.3 + 0.25*sin(5*pi*a) }
  #declare BarkRd = function(a) { 1.25*pi*cos(5*pi*a) }
  #declare BarkRref = 0.3;
#end
#declare BarkSpeed = function(a) { sqrt(BarkL*BarkL + pow(BarkRd(a), 2)) }
#declare BarkS = function(a) { a/96*(BarkSpeed(0) + BarkSpeed(a) + sum(i, 1, 31, (3 - cos(pi*i))*BarkSpeed(i*a/32))) }

#switch (Method)
  #case (0)
    #declare BarkTX = function(a, b, x, y, z) { 2*pi*BarkK*BarkRref*a }
    #declare BarkTY = function(a, b, x, y, z) { 0 }
    #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*BarkL*b }
  #break
  #case (1)
  #case (5)
    #declare BarkTX = function(a, b, x, y, z) { BarkK*BarkRref*cos(2*pi*a) }
    #declare BarkTY = function(a, b, x, y, z) { BarkK*BarkRref*sin(2*pi*a) }
    #if (Method = 1)
      #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*BarkL*b }
    #else
      #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*BarkS(b) }
    #end
  #break
  #case (2)
  #case (3)
    #declare BarkTX = function(a, b, x, y, z) { BarkK*BarkR(b)*cos(2*pi*a) }
    #declare BarkTY = function(a, b, x, y, z) { BarkK*BarkR(b)*sin(2*pi*a) }
    #if (Method = 2)
      #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*BarkL*b }
    #else
      #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*BarkS(b) }
    #end
  #break
  #case (4)
    #declare BarkTX = function(a, b, x, y, z) { BarkK*x }
    #declare BarkTY = function(a, b, x, y, z) { -BarkK*z }
    #declare BarkTZ = function(a, b, x, y, z) { BarkK/BarkG*y }
  #break
#end

#declare BarkCracks = function { pattern { crackle } }
#declare BarkPattern = function(a, b, x, y, z) { BarkCracks(BarkTX(a, b, x, y, z), BarkTY(a, b, x, y, z), BarkTZ(a, b, x, y, z)) }
#declare BarkCrack = function(a, b, x, y, z) {
  -BarkDepth*BarkR(b)*min(1, 20*b, 20*(1 - b))*max(0, 1 - BarkPattern(a, b, x, y, z)/BarkWidth)
}

#declare BarkRho = BarkL/radians(BarkBend);
#declare BarkStart = -radians(BarkBend)/2;
#macro BarkAxisAt(V)
  #local Th = BarkStart + V*BarkL/BarkRho;
  <BarkRho*(cos(BarkStart) - cos(Th)), BarkRho*(sin(Th) - sin(BarkStart)), 0>
#end
#macro BarkTangentAt(V)
  #local Th = BarkStart + V*BarkL/BarkRho;
  <sin(Th), cos(Th), 0>
#end
#declare BarkHandle = 4/3*tan(radians(BarkBend)/8)*BarkRho;

#macro BarkSurface(U, V)
  #if (Shape = 3)
    #local T = BarkTangentAt(V);
    #local P = BarkAxisAt(V) + BarkR(V)*(z*cos(2*pi*U) + <T.y, -T.x, 0>*sin(2*pi*U));
  #else
    #local P = <BarkR(V)*cos(2*pi*U), BarkL*V, -BarkR(V)*sin(2*pi*U)>;
  #end
  P
#end

#macro BarkSkein()
  skein {
    expressions {
      #if (Shape = 3)
        scale <1, BarkL, 1>
        extrude { radius function(v) { BarkR(v) } }
        bend {
          axis y
          along path {
            BarkAxisAt(0) handle BarkHandle*BarkTangentAt(0),
            BarkAxisAt(0.5) handle BarkHandle*BarkTangentAt(0.5),
            BarkAxisAt(1) handle BarkHandle*BarkTangentAt(1)
            arclength
          }
        }
      #else
        scale <1, BarkL, 1>
        extrude { radius function(v) { BarkR(v) } }
        #if (TwistInFold) rotate { axis y  angle function(v) { Twist*v } } #end
      #end
      displace function(uv, pos) { BarkCrack(uv.u, uv.v, pos.x, pos.y, pos.z) }
      #if ((Shape != 3) & !TwistInFold & (Twist != 0))
        rotate { axis y  angle function(v) { Twist*v } }
      #end
    }
    closed u  ends flat
  }
#end

#declare BarkTexture = texture { pigment { rgb <0.42, 0.3, 0.2> } finish { phong 0.15 } }
