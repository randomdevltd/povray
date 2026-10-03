// Straight light through a mouth and out of its far mouth, from above, -A File_Gamma=1.0: 1 = 0 (open front, solid disc),
// 2 = 9 (closed back passes), 3 = mean of 0 and 5 (half open), 4 = 5 (front off), 6 = 7 in the shadow (far back open), 8 = 10 off the ball (exit).
#version 3.8;
#ifndef (Case) #declare Case = 1; #end

global_settings { assumed_gamma 1.0 }
camera { orthographic location <0, 10, 0> look_at 0 right 8 * x up 6 * z }
light_source { <-6, 12, 1> rgb 0.15 shadowless }
box { <-10, -1, -10>, <10, 0, 10> pigment { checker rgb 0.9 rgb 0.6 } finish { diffuse 1 ambient 0 } }

#declare Lamp = <0, 3, ((Case = 2 | Case = 9) ? 3 : -3)>;
#if (Case < 6 | Case = 9)
    light_source { Lamp rgb 1 }
#else
    light_source { <0.5, 4, -1.5> rgb 1 }
#end
#declare Door = disc { <0, 1, 0>, -z, 0.8 };
#declare Ball = sphere { <0, 1, 0>, 0.8 };
#declare Away = transform { translate 1000 * x };
#switch (Case)
    #case (0) object { Door pigment { rgb 0.2 } } #break
    #case (1) portal { Door to { transform Away } near { no_lights } } #break
    #case (2) portal { Door to { transform Away } near { no_lights } } #break
    #case (3) portal { Door to { transform Away } near { no_lights } pigment { rgbt <1, 1, 1, 0.5> } } #break
    #case (4) portal { Door to { transform Away } near { no_lights } front off back on } #break
    #case (6) portal { Ball to { transform Away } near { no_lights } far { back on } } #break
    #case (7) object { Ball pigment { rgb 0.2 } } #break
    #case (8) portal { Ball to { translate -y scale 2 translate y } exit no_lights } #break
#end
