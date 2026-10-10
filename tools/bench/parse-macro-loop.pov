// A 100,000-pass #while loop calling a macro: parse-dominated at a tiny image size.
#version 3.7;
global_settings { assumed_gamma 1 }
#macro Step(A, B)
  #local C = A * 0.5 + B;
  #if (C > 1000)
    #local C = C - 1000;
  #end
  C
#end
#declare Acc = 0;
#declare I = 0;
#while (I < 100000)
  #declare Acc = Step(Acc, I);
  #declare I = I + 1;
#end
camera { location <0, 0, -4> look_at 0 }
light_source { <5, 5, -5> rgb 1 }
sphere { 0, 1 pigment { rgb mod(Acc, 1000) / 1000 } }
