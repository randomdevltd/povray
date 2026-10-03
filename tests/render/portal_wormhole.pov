// A portal and its far mouth join two tunnel mouths that neither line up nor face each other: View 1 looks down the red tunnel and
// sees the blue, View 2 the reverse through the image's back. View 3, a mouth with its image behind it, nests to the fallback.
#version 3.8;
#ifndef (View) #declare View = 1; #end
global_settings { assumed_gamma 1.0 max_trace_level 8 }

#declare ToB = transform { rotate <0, 110, 0> rotate <15, 0, 0> translate <6, 1.5, 4> };
#macro Tunnel(Colour)
    union
    {
        box { <-1.1, -1.1, 0>, <-1, 1.1, 5> }
        box { <1, -1.1, 0>, <1.1, 1.1, 5> }
        box { <-1, -1.1, 0>, <1, -1, 5> }
        box { <-1, 1, 0>, <1, 1.1, 5> }
        sphere { <0, -0.6, 2.5>, 0.4 }
        pigment { rgb Colour }
    }
#end

#switch (View)
    #case (1) camera { location <1.2, 0.8, -4> look_at <0, 0, 0> angle 50 } #break
    #case (2) camera { location <-1.2, 0.8, -4> look_at <0, 0, 0> angle 50 transform ToB } #break
    #else camera { location <0.3, 0.2, -0.25> look_at <0.1, 0.1, 1> angle 100 }
#end
light_source { <-3, 10, -8> rgb 1 }
background { rgb <0.6, 0.75, 0.9> }
plane { y, -3 pigment { checker rgb 0.7 rgb 0.4 } }

#declare Mouth = polygon { 5, <-1, -1, 0>, <-1, 1, 0>, <1, 1, 0>, <1, -1, 0>, <-1, -1, 0> };
#if (View <= 2)
    object { Tunnel(<0.9, 0.2, 0.15>) }
    object { Tunnel(<0.15, 0.3, 0.9>) transform ToB }
    portal { Mouth to { transform ToB } far { front off back on } }
#else
    object { Tunnel(<0.9, 0.2, 0.15>) }
    portal { Mouth to { translate -0.5 * z } max_trace_level 20 fallback { rgb <1, 0, 1> } }
#end
