// Three doorways onto the sea seen from the front (lower row) and the same three seen from behind (upper row): left the
// default (front on, back off), middle front off back on, right both open. A closed side is not there: the wall shows.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 2.6, -6.6> look_at <0, 1.3, 0> angle 50 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-3, 0, 2.2>, <3, 1.5, 4.4> pigment { rgb <0.7, 0.68, 0.62> } }
box { <-4, 0, 5.4>, <4, 4.5, 5.6> pigment { brick rgb 0.75, rgb <0.65, 0.3, 0.2> scale 0.08 } }

#declare Far = <1000, 0, 0>;
Elsewhere(Far)
#macro Doorway(Place, Turn, Front, Back)
    object { Frame(1.0, 1.3, <0.2, 0.3, 0.28>) rotate Turn * y translate Place }
    portal { object { Door(1.0, 1.3) rotate Turn * y translate Place } to { translate Far - Place } front Front back Back far off }
#end
#for (I, 0, 2)
    #local X = -1.8 + 1.8 * I;
    Doorway(<X, 0, 0>, 0, (I != 1), (I != 0))
    Doorway(<X, 1.5, 3.3>, 180, (I != 1), (I != 0))
#end
