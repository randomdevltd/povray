// Lights through portals. Case 1: B's mouth leads out of A's, so a candle shining into A, and an L block's shadow,
// spill out of B onto the floor and wall. Case 2: the far mouth open at its back; a candle in each box lights the other.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (NoLights) #declare NoLights = 0; #end    // 1: the portal carries no light
#ifndef (Far) #declare Far = 1; #end              // 1: the L block between the candle and A's mouth
#ifndef (Near) #declare Near = 0; #end            // 1: a post between B's mouth and the floor it lights
#ifndef (Light) #declare Light = 0; #end          // 0: point light; 1: area light; 2: spotlight
#ifndef (Jitter) #declare Jitter = 1; #end
#ifndef (Open) #declare Open = 0; #end            // pigment 1: tinted; 2: half open; 3: shut
#ifndef (Fill) #declare Fill = 0.15; #end
#ifndef (Extra) #declare Extra = 0; #end          // more lamps about the room, to measure cost
global_settings { assumed_gamma 1.0 }

camera { location <-1.5, 7, -6> look_at <0.6, 0.6, 2.6> angle 64 }
#if (Fill > 0) light_source { <-2, 12, -10> rgb Fill shadowless } #end
#for (I, 1, Extra) light_source { <-7 + 2.5 * I, 4.5, -6 + I> rgb 0.08 } #end
plane { y, 0 pigment { checker rgb 0.62 rgb 0.5 } }
box { <-12, 0, 8>, <12, 6, 8.2> pigment { rgb 0.75 } }
box { <5, 0, -8>, <5.2, 6, 8> pigment { rgb 0.75 } }

#declare TA = transform { rotate y * 20 translate <-3, 0, 0> };
#declare TB = transform { rotate y * -90 translate <0.5, 0, 4.5> };
#declare Mouth = polygon { 5, <-1, 0.05, 0>, <-1, 2.05, 0>, <1, 2.05, 0>, <1, 0.05, 0>, <-1, 0.05, 0> };
#macro Shell(Colour)
    difference { box { <-1.1, 0, 0>, <1.1, 2.15, 3.1> } box { <-1, 0.05, -0.1>, <1, 2.05, 3> } pigment { rgb Colour } }
#end
#declare Ell = union { box { <-0.35, 0.75, -0.05>, <-0.2, 1.7, 0.05> } box { <-0.35, 0.75, -0.05>, <0.45, 0.9, 0.05> } };
#macro Candle(Place, Colour)
    light_source
    {
        Place rgb Colour fade_distance 2 fade_power 2
        #switch (Light)
            #case (1) area_light x * 0.8, y * 0.8, 6, 6 adaptive 1 #if (Jitter) jitter #end #break
            #case (2) spotlight point_at Place * <1, 1, 0> radius 9 falloff 14 #break
        #end
    }
#end

object { Shell(<0.85, 0.3, 0.2>) transform TA }
object { Shell(<0.25, 0.4, 0.85>) transform TB }
#switch (Case)
    #case (1)
        union
        {
            Candle(<0, 1.1, -2.4>, <1, 0.75, 0.45> * 3)
            #if (Far) object { Ell translate z * -1.2 pigment { rgb 0.3 } } #end
            transform TA
        }
        #if (Near) cylinder { <0.3, 0, -1.2>, <0.3, 2.6, -1.2>, 0.12 pigment { rgb 0.3 } transform TB } #end
        portal
        {
            Mouth to { rotate y * 180 transform TA } far off
            #switch (Open)
                #case (1) pigment { rgb <1, 0.35, 0.15> } #break
                #case (2) pigment { rgbt <1, 1, 1, 0.5> } #break
                #case (3) pigment { rgbt <1, 1, 1, 1> } #break
            #end
            #if (NoLights) no_lights #end
            transform TB
        }
    #break
    #case (2)
        union { Candle(<0.5, 1.5, 2.6>, <1, 0.6, 0.25> * 2) object { Ell translate z * 1.4 } pigment { rgb 0.3 } transform TA }
        union { Candle(<-0.5, 0.9, 2.6>, <0.3, 0.55, 1> * 2) object { Ell scale <-1, 1, 1> translate z * 1.4 } pigment { rgb 0.3 } transform TB }
        portal { Mouth to { transform TB } far { front off back on } #if (NoLights) no_lights #end transform TA }
    #break
#end
