// skein_bark_check.pov: prints each bark method's texture scale and grain against the surface, and the crack pattern either side of the u seam.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare H = 1e-5;
#declare Names = array[6] { "control: u straight in", "fixed circle", "tracking, linear z", "tracking, arclength z", "pos", "fixed circle, arclength z" }

#macro Tex(U, V)
  #local S = BarkSurface(U, V);
  <BarkTX(U, V, S.x, S.y, S.z), BarkTY(U, V, S.x, S.y, S.z), BarkTZ(U, V, S.x, S.y, S.z)>
#end

#macro Row(U, V)
  #local Su = (BarkSurface(U + H, V) - BarkSurface(U - H, V))/(2*H);
  #local Sv = (BarkSurface(U, V + H) - BarkSurface(U, V - H))/(2*H);
  #local Tu = (Tex(U + H, V) - Tex(U - H, V))/(2*H);
  #local Tv = (Tex(U, V + H) - Tex(U, V - H))/(2*H);
  #local E1 = vnormalize(Su);
  #local B = vdot(Sv, E1)/vlength(Su);
  #local C = vlength(Sv - vdot(Sv, E1)*E1);
  #local A1 = Tu/vlength(Su);
  #local A2 = (Tv - B*Tu)/C;
  #local Q = vdot(A1, A2);
  #local D = vdot(A1, A1) - vdot(A2, A2);
  #local Tilt = 0;
  #if (abs(Q) + abs(D) > 1e-9) #local Tilt = degrees(0.5*atan2(2*Q, D)); #end
  #local SU = vlength(Tu)/vlength(Su)/BarkK;
  #local SV = vlength(Tv)/vlength(Sv)*BarkG/BarkK;
  #debug concat("| ", str(V, 0, 2), " | ", str(U, 0, 2), " | ", str(BarkR(V), 0, 3), " | ", str(SU, 0, 3), " | ", str(SV, 0, 3),
                " | ", str(SU/SV, 0, 3), " | ", str(Tilt, 0, 1), " |\n")
#end

#for (Gi, 0, 1)
  #declare BarkG = (Gi = 0 ? 2.5 : 1);
  #for (Sh, 1, 3 - Gi)
    #for (Me, 1, 5)
      #declare Shape = Sh;
      #declare Method = Me;
      #include "skein_bark.inc"
      #debug concat("\nshape ", str(Sh, 0, 0), ", ", Names[Me], ", grain ", str(BarkG, 0, 1), "\n| v | u | R | u scale | v scale | grain | tilt |\n")
      #if (Sh = 3)
        #for (J, 0, 3)
          #for (I, 0, 2)
            Row(select(I - 1, 0, 0.25, 0.75), select(J - 1, 0.1, 0.2, select(J - 2, 0, 0.5, 0.9)))
          #end
        #end
      #else
        #for (J, 1, 9) Row(0.3, J/10) #end
      #end
    #end
  #end
#end

#declare BarkG = 2.5;
#debug "\nseam: largest change across u = 0/1 over 1001 values of v, texture point and crack pattern\n"
#for (Sh, 1, 3)
  #for (Me, 0, 5)
    #declare Shape = Sh;
    #declare Method = Me;
    #include "skein_bark.inc"
    #declare DT = 0;
    #declare DP = 0;
    #for (J, 0, 1000)
      #declare V = J/1000;
      #declare S0 = BarkSurface(0, V);
      #declare S1 = BarkSurface(1, V);
      #declare DT = max(DT, vlength(Tex(0, V) - Tex(1, V)));
      #declare DP = max(DP, abs(BarkPattern(0, V, S0.x, S0.y, S0.z) - BarkPattern(1, V, S1.x, S1.y, S1.z)));
    #end
    #debug concat("shape ", str(Sh, 0, 0), ", ", Names[Me], ": texture point ", str(DT, 0, 15), ", pattern ", str(DP, 0, 15), "\n")
  #end
#end

camera { location <0, 0, -2> look_at 0 }
background { rgb 0 }
