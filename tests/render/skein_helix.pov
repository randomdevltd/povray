// skein_helix.pov: a double helix, two straight tubes bent along helical paths and a ladder of rungs, unioned.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare R = 0.6;
#declare H = 4;
#declare Turns = 1.5;
// a helix of constant speed, so the tube that bends along it is that long
#declare Climb = sqrt(pow(2*pi*Turns*R, 2) + H*H);
#macro Strand(Phase)
  skein {
    expressions {
      scale <1, Climb, 1>
      extrude { radius 0.08 }
      bend {
        axis y
        along function(t) { R*cos(2*pi*Turns*t) }, function(t) { H*t }, function(t) { R*sin(2*pi*Turns*t) }
      }
    }
    closed u  ends flat
    rotate y*Phase
  }
#end
#macro Rung(Y)
  skein {
    expressions {
      extrude { radius 0.035 }
    }
    closed u  ends flat
    translate <0, -0.5, 0>  scale <1, 2*(R - 0.1), 1>
    rotate z*-90  rotate y*(-360*Turns*Y/H)  translate <0, Y, 0>
  }
#end
#declare Helix = union {
  object { Strand(0) pigment { rgb <0.85, 0.3, 0.25> } }
  object { Strand(180) pigment { rgb <0.25, 0.45, 0.85> } }
  #for (Y, 0.15, H - 0.1, 0.27)
    object { Rung(Y) pigment { rgb <0.9, 0.85, 0.7> } }
  #end
}

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
camera { location <0, 2, -8.6> look_at <0, 2, 0> angle 45 }
object { Helix  finish { phong 0.5 }  rotate z*-20 }
