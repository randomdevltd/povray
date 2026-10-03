// One lamp behind three discs: a plain disc, a portal disc with its far mouth (the default) and one with far off. Light that
// enters the middle mouth comes out of its far mouth, far away, so it casts a shadow; the right one lets the light pass.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 6.4, -6.4> look_at <0, 0.3, 1.2> angle 50 }
light_source { <0, 4, -3> rgb 1.2 }
light_source { <-6, 12, -10> rgb 0.15 shadowless }
Sky()
Floor()

#declare Far = <1000, 0, 0>;
Elsewhere(Far)
#declare Disc = disc { <0, 1, 0>, -z, 0.8 };
object { Disc translate -2.2 * x pigment { rgb 0.9 } }
portal { Disc to { translate Far } }
portal { object { Disc translate 2.2 * x } to { translate Far + <30, 0, 0> - 2.2 * x } far off }
#for (I, -1, 1)
    cylinder { <2.2 * I, 0, 0.02>, <2.2 * I, 0.2, 0.02>, 0.03 pigment { rgb 0.3 } }
#end
