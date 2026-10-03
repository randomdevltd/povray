// The same view on two sets; on the right a perturb pigment shifts each row sideways, like a set losing its tracking.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 0.8, -5.4> look_at <0, 0.65, 0> angle 48 }
light_source { <-4, 6, -6> rgb 1 }
light_source { <96, 8, -10> rgb 1 }
Sky()
Floor()
box { <-6, 0, 1.2>, <6, 4, 1.3> pigment { rgb <0.85, 0.82, 0.75> } }

// The set's subject, far away: striped posts on a checker floor.
#for (I, -3, 3)
    cylinder { <100 + 0.7 * I, 0, 2 + 0.3 * mod(I + 3, 2)>, <100 + 0.7 * I, 2.4, 2 + 0.3 * mod(I + 3, 2)>, 0.18
        pigment { gradient y color_map { [0.5 rgb <0.9, 0.2, 0.1>] [0.5 rgb 0.95] } scale 0.4 } }
#end
#declare View = camera { location <100, 1.2, -3> look_at <100, 1, 2> right x * 4 / 3 angle 55 };
#declare Plain = pigment { screen { camera { View } } };
#declare Wobble = pigment
{
    screen
    {
        camera { View }
        perturb { user_defined { function { 0.03 * sin(y * 60) }, function { 0 }, function { 0 } } }
    }
};
object { TV(1.6, 1.2, Plain) translate <-1.15, 0.1, 0> }
object { TV(1.6, 1.2, Wobble) translate <1.15, 0.1, 0> }
