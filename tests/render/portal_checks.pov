// Portal contract, rendered with -A File_Gamma=1.0: Check 1 = 2 (the inverse map), 3 = mean of 4 and 5 (pigment), 6 = 7 (absent from behind).
// 8: a camera inside a closed body warns and terminates. 9, 10: a portal leading in front of itself stops at its limit, absent or fallback.
#version 3.8;
#ifndef (Check) #declare Check = 1; #end  // 11: a strongly perturbed view stays inside the surface and terminates; 12 = 13 (union body)
global_settings { assumed_gamma 1.0 max_trace_level 10 }

#declare Glow = finish { emission 1 diffuse 0 ambient 0 };
#declare Content = union
{
    box { -30, 30 inverse pigment { checker rgb <0.2, 0.3, 0.6> rgb <0.9, 0.8, 0.5> scale 3 } finish { Glow } }
    sphere { <-2, 0, 8>, 1.5 pigment { gradient y color_map { [0 rgb <1, 0.2, 0.1>] [1 rgb <0.1, 1, 0.3>] } scale 3 translate -1.5 * y } finish { Glow } }
    box { <1, -2, 6>, <3, 1, 12> rotate <10, 25, 0> pigment { checker rgb 1 rgb 0.1 scale 0.5 } finish { Glow } }
    torus { 1.2, 0.3 rotate <70, 0, 0> translate <0.5, 2, 10> pigment { rgb <0.9, 0.5, 1> } finish { Glow } }
};
#declare Map = transform { scale <2, 1, 0.5> rotate <20, 35, -10> translate <1000, 50, -300> };
#declare Quad = polygon { 5, <-10, -10, 1>, <-10, 10, 1>, <10, 10, 1>, <10, -10, 1>, <-10, -10, 1> };

#if (Check <= 7)
    #if (Check = 6 | Check = 7)
        camera { location <0, 0, 12> look_at <0, 0, -1> angle 70 }
    #else
        camera { location 0 look_at z angle 70 }
    #end
    #if (Check != 2)
        object { Content transform Map }
        box { <-200, -200, 100>, <200, 200, 101> pigment { gradient x color_map { [0 rgb 0.1] [1 rgb <0.8, 0.4, 0.1>] } scale 40 } finish { Glow } }
        box { <-200, -200, -101>, <200, 200, -100> pigment { rgb <0.3, 0.6, 0.3> } finish { Glow } }
    #end
    #if (Check = 1 | Check = 6)
        #declare P = portal { object { Quad translate -5 * z } to { translate 5 * z transform Map } far off };
        object { P translate 5 * z }
    #end
    #if (Check = 2)
        object { Content }
    #end
    #if (Check >= 3 & Check <= 5)
        portal { Quad to { transform Map } far off pigment { rgbt <1, 1, 1, select(Check - 4, 0.5, 0, 1)> } }
    #end
#elseif (Check = 12 | Check = 13)
    camera { location 0 look_at z angle 70 }
    object { Content transform Map }
    #macro Pair() sphere { <-0.8, 0, 4>, 1.5 } sphere { <0.8, 0, 4>, 1.5 } #end
    #if (Check = 12) #declare Body = union { Pair() } #else #declare Body = merge { Pair() } #end
    portal { Body to { transform Map } far off pigment { rgbt <1, 1, 1, 0.5> } }
#elseif (Check = 11)
    camera { location 0 look_at z angle 70 }
    object { Content transform Map }
    portal { Quad to { transform Map } far off perturb { normal { bumps 1 scale 0.5 } 4 } }
#elseif (Check = 8)
    camera { location 0 look_at z angle 70 }
    portal { sphere { 0, 2 } to { translate 1000 * x } far off }
    object { Content translate 1000 * x }
#else
    camera { location <0, 0, -6> look_at 0 angle 50 }
    light_source { <-5, 8, -10> rgb 1 }
    background { rgb <0.4, 0.5, 0.7> }
    box { <-20, -2, -20>, <20, -1.9, 20> pigment { checker rgb 0.8 rgb 0.3 } }
    sphere { <1.4, -0.9, 0>, 1 pigment { rgb <0.9, 0.3, 0.2> } }
    #if (Check = 9)
        portal { box { -1, 1 } to { translate -0.5 * z } far off max_trace_level 256 }
    #else
        portal { box { -1, 1 } to { translate -0.5 * z } far off max_trace_level 12 fallback { rgb <1, 0, 1> } }
    #end
#end
