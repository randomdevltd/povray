// A translation-only portal against the real thing, seen from above: Mode 0 lights the floor through a portal in a wall's
// hole from a lamp in a closed room far away; Mode 1 puts the lamp and its blocker at their images, behind the open hole.
#version 3.8;
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Light) #declare Light = 0; #end          // 0: point; 1: area; 2: spot; 3: parallel; 4: cylinder
#ifndef (Jitter) #declare Jitter = 0; #end
#ifndef (NoLights) #declare NoLights = 0; #end    // a parallel lamp reaches the floor straight too: compare the difference
global_settings { assumed_gamma 1.0 }

camera { location <0, 10, -3.6> look_at <0, 0, -3.6> sky z angle 50 }
plane { y, 0 pigment { checker rgb 0.85 rgb 0.7 } finish { ambient 0 diffuse 1 } }

#declare Far = <100, 0, 0>;
#declare Wall = difference { box { <-6, 0, 0>, <6, 5, 0.2> } box { <-1, 1, -1>, <1, 3, 1> } pigment { rgb 0.7 } };
#declare Lamp = light_source
{
    <0.3, 3.6, 2.5> rgb 3 fade_distance 2 fade_power 2
    #switch (Light)
        #case (1) area_light x * 1.2, y * 1.2, 7, 7 adaptive 1 #if (Jitter) jitter #end #break
        #case (2) spotlight point_at <-0.4, 0, -4> radius 12 falloff 16 #break
        #case (3) parallel point_at <-0.4, 0, -4> #break
        #case (4) cylinder point_at <-0.4, 0, -4> radius 0.6 falloff 0.9 #break
    #end
};
#declare Block = box { <-0.6, -0.1, -0.1>, <0.6, 0.1, 0.1> rotate z * 30 translate <-0.1, 2.5, 1.3> pigment { rgb 0.5 } };

object { Wall }
#if (Mode = 0)
    portal { polygon { 5, <-1, 1, 0>, <-1, 3, 0>, <1, 3, 0>, <1, 1, 0>, <-1, 1, 0> } to { translate Far } far off #if (NoLights) no_lights #end }
    union
    {
        object { Lamp }
        object { Block }
        object { Wall }
        difference { box { <-4, -1, -1>, <4, 7, 6> } box { <-3.9, -0.9, -0.9>, <3.9, 6.9, 5.9> } pigment { rgb 0.5 } }
        translate Far
    }
#else
    object { Lamp }
    object { Block }
#end
