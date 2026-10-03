// Straight light needs a matched pair: 1, 3, 4, 5 = 9 (far off, far front off, far no_lights, volume far off: no shadow);
// 2 = 0 in the shadow (far mouth carries it); 5 shows the lit box through the ball. 6 = 9 and 7 = 1, ray counts too (no open side).
#version 3.8;
#ifndef (Case) #declare Case = 1; #end

global_settings { assumed_gamma 1.0 }
camera { orthographic location <0, 10, 0> look_at 0 right 8 * x up 6 * z }
light_source { <-6, 12, 1> rgb 0.15 shadowless }
box { <-10, -1, -10>, <10, 0, 10> pigment { checker rgb 0.9 rgb 0.6 } finish { diffuse 1 ambient 0 } }
light_source { <0, 3, -3> rgb 1 }

#declare Away = transform { translate 1000 * x };
light_source { <0, 6, -2> rgb 1 transform Away }
box { <-0.3, 0.2, -0.3>, <0.3, 0.8, 0.3> rotate 30 * y transform Away pigment { rgb <0.9, 0.2, 0.15> } }
#declare Door = disc { <0, 1, 0>, -z, 0.8 };
#declare Ball = sphere { <0, 1, 0>, 0.8 };
#switch (Case)
    #case (0) object { Door pigment { rgb 0.2 } } #break
    #case (1) portal { Door to { transform Away } near { no_lights } far off } #break
    #case (2) portal { Door to { transform Away } near { no_lights } } #break
    #case (3) portal { Door to { transform Away } near { no_lights } far { front off back on } } #break
    #case (4) portal { Door to { transform Away } near { no_lights } far { no_lights } } #break
    #case (5) portal { Ball to { transform Away } near { no_lights } far off } #break
    #case (6) portal { Ball to { transform Away } no_lights front off far off } #break
    #case (7) portal { Door to { transform Away } near { no_lights } far { front off } } #break
#end
