#version 4.0;
#include "scene.inc"
#declare Paint = texture { pigment { rgb <0.9, 0.3, 0.3> } }
union { sphere { <-1, 0, 0>, 0.3 texture { Paint } } sphere { <1, 0, 0>, 0.3 texture { Paint } } translate <0, 1.5, -1> }
sphere { <2, 0.5, -1>, 0.2 } sphere { <2.5, 1, -1>, 0.2 }
sphere { <-2, 0, -2>, 0.2 }
#declare Height = 0.25;
#declare Base = array[3] { 1, 2, 3 };
#declare Mixed = array mixed[3] { 1, <1, 2, 3>, "s" };
#declare Layer = texture { pigment { rgb 1 } } texture { pigment { rgbt <0, 0, 1, 0.5> } }
#include "review_reads.inc"
#declare Ticks = 0;
union { Count() Count() pigment { rgb z } }
union { Count() #declare Ticks = 5; Count() pigment { rgb x } }
#declare Tint = rgb <0.1, 0.4, 0.6>;
sphere { <Ticks * 0.3, -1, 2>, 0.3 pigment { Tint } }
union {
  Stack()
  #declare Height = 0.75;
  Stack()
  translate <2, 0, 1>
}
sphere { <-2, 0, 2>, ReadSize * 0.5 pigment { rgb x } }
sphere { <0, 0, 2>, 0.2 }
#for (I, 0, 3) sphere { <1.5 * cos(I * tau / 4), 0.2, 1.5 * sin(I * tau / 4)>, 0.1 } #end
box { <2.5, -1, -1>, <3, 0, 0> texture { Layer } }
sphere { <-3, 1, 0>, 0.25 }
sphere { <0, 0.5, -2>, 0.15 pigment { rgb y } }
