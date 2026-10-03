// Two doorways in a room's walls, joined by one map: in through A's front, out of B's front into the room, and back.
// Views: 1 into A, 2 into B, 3 at A from outside the room, 4 at B from outside, 5 from above (Lit 1: a candle by each).
#version 3.8;
#ifndef (View) #declare View = 1; #end
#ifndef (TwoWay) #declare TwoWay = 1; #end        // 0: A alone (far off); 1: B's sides set; 2: B by default
#ifndef (AF) #declare AF = 1; #end                // the front and back of A, then of B
#ifndef (AB) #declare AB = 0; #end
#ifndef (BF) #declare BF = 1; #end
#ifndef (BB) #declare BB = 0; #end
#ifndef (Lit) #declare Lit = 0; #end
#ifndef (NoLights) #declare NoLights = 0; #end
global_settings { assumed_gamma 1.0 }

#switch (View)
    #case (1) camera { location <1.5, 1.5, -2.5> look_at <-5, 1, 0> angle 55 } #break
    #case (2) camera { location <1.8, 1.5, 1> look_at <0, 1, 5> angle 55 } #break
    #case (3) camera { location <-8.5, 1.6, -3> look_at <-5, 1, 0> angle 50 } #break
    #case (4) camera { location <1.5, 1.6, 8.5> look_at <0, 1, 5> angle 50 } #break
    #else camera { location <-2.2, 9.5, 2.1> look_at <-2.2, 0, 2.1> sky z angle 50 }
#end
background { rgb <0.6, 0.75, 0.9> }
plane { y, 0 pigment { checker rgb 0.75 rgb 0.6 } }

#macro Wall(Lo, Hi, Hole, Colour)
    difference { box { Lo, Hi } #if (vlength(Hole) > 0) box { Hole - <0.6, 0.1, 0.6>, Hole + <0.6, 2, 0.6> } #end pigment { rgb Colour } }
#end
Wall(<-5.2, 0, -5.2>, <-5, 3, 5.2>, <-5.1, 0, 0>, <0.8, 0.3, 0.25>)
Wall(<-5.2, 0, 5>, <5.2, 3, 5.2>, <0, 0, 5.1>, <0.25, 0.4, 0.85>)
Wall(<5, 0, -5.2>, <5.2, 3, 5.2>, 0, <0.3, 0.65, 0.3>)
Wall(<-5.2, 0, -5.2>, <5.2, 3, -5>, 0, <0.85, 0.8, 0.6>)
box { <-12.2, 0, -12>, <-12, 4, 12> pigment { rgb <1, 0.55, 0.1> } }
box { <-12, 0, 12>, <12, 4, 12.2> pigment { rgb <0.1, 0.8, 0.85> } }
sphere { <-3.4, 0.4, 0.9>, 0.4 pigment { rgb <0.95, 0.85, 0.1> } }
cylinder { <-0.9, 0, 3.3>, <-0.9, 1.3, 3.3>, 0.3 pigment { rgb <0.8, 0.2, 0.7> } }

#if (Lit)
    light_source { <0, 30, 0> rgb 0.1 shadowless }
    light_source { <-4.4, 1.4, -1.5> rgb <1, 0.45, 0.1> * 2.5 fade_distance 1.2 fade_power 2 }
    light_source { <1.5, 1.4, 4.4> rgb <0.15, 0.45, 1> * 2.5 fade_distance 1.2 fade_power 2 }
    box { <-4.7, 0, -0.85>, <-4.6, 1.8, -0.75> pigment { rgb 0.3 } }
    box { <0.75, 0, 4.6>, <0.85, 1.8, 4.7> pigment { rgb 0.3 } }
#else
    light_source { <-10, 30, -20> rgb 0.8 }
    light_source { <1, 2.8, 0> rgb 0.4 }
    light_source { <15, 25, 25> rgb 0.3 shadowless }
#end

portal
{
    polygon { 5, <-0.6, 0, 0>, <-0.6, 2, 0>, <0.6, 2, 0>, <0.6, 0, 0>, <-0.6, 0, 0> rotate y * -90 translate <-4.95, 0, 0> }
    to { rotate y * -90 translate z * 9.9 }
    near { front AF back AB }
    #switch (TwoWay)
        #case (0) far off #break
        #case (1) far { front BF back BB } #break
    #end
    #if (NoLights) no_lights #end
}
