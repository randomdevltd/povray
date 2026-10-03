// Screen contract. Check=1 with +UA: the panel shows its camera's background opaquely. Check=2: the panel casts a shadow.
// Check=3: a screen at the end of a mirror tube shows the tube, at limit 256 and max_trace_level 256; it must terminate.
#version 3.8;
#ifndef (Check) #declare Check = 1; #end
global_settings { assumed_gamma 1.0 max_trace_level #if (Check = 3) 256 #else 5 #end }
background { rgbt <0.2, 0.4, 0.9, (Check = 1)> }

#if (Check = 3)
    #declare Tube = camera { location 0 look_at z right x * 4 / 3 angle 60 }
    camera { Tube }
    union
    {
        box { <1, -0.75, 0>, <1.01, 0.75, 200> }
        box { <-1.01, -0.75, 0>, <-1, 0.75, 200> }
        box { <-1, 0.75, 0>, <1, 0.76, 200> }
        box { <-1, -0.76, 0>, <1, -0.75, 200> }
        pigment { rgb 0 }
        finish { reflection 1 diffuse 0 }
    }
    box { <-1, -0.75, 200>, <1, 0.75, 200.01>
        pigment { screen { camera { Tube } max_trace_level 256 } scale <2, 1.5, 1> translate <-1, -0.75, 0> }
        finish { emission 1 diffuse 0 }
    }
#else
    camera { location <0, 1.5, -5> look_at <0, 0.9, 0> angle 45 }
    light_source { <4, 5, 4> rgb 1 }
    #if (Check = 2)
        plane { y, 0 pigment { rgb 0.8 } }
    #end
    box { <-1, 0.2, -0.01>, <1, 1.7, 0>
        pigment
        {
            screen { camera { location <0, 0, -100> look_at <0, 0, -200> right x * 4 / 3 } }
            scale <2, 1.5, 1>
            translate <-1, 0.2, 0>
        }
        finish { emission 1 diffuse 0 }
    }
#end
