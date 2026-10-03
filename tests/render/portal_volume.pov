// A portal sphere is entered from any side: views 1-3, one-way, show the lit red box; 4 and 5, two-way, the lit cone.
#version 3.8;
#ifndef (View) #declare View = 1; #end
#ifndef (TwoWay) #declare TwoWay = (View > 3); #end
global_settings { assumed_gamma 1.0 }

#declare Far = <30, 0, 0>;
#declare Eye = array[5] { <0.5, 0.8, -4>, <4, 1.2, 0.8>, <-0.8, 4.2, 0.5>, <-0.5, 0.8, -4>, <-3, 2.8, 2.5> };
#declare At = <0, 0, 0>;
#if (View > 3) #declare At = Far; #end
camera { location Eye[View - 1] + At look_at At angle 50 }
light_source { <-10, 20, -15> rgb 1 }
background { rgb <0.6, 0.75, 0.9> }
plane { y, -1.6 pigment { checker rgb 0.75 rgb 0.6 } }

cone { <0, -0.6, 0>, 0.5, <0, 0.7, 0>, 0 pigment { rgb <0.2, 0.35, 0.95> } }
box { -0.45, 0.45 rotate <30, 40, 0> translate Far pigment { rgb <0.9, 0.2, 0.15> } }
portal { sphere { 0, 1.2 } to { translate Far } #if (!TwoWay) far off #end }
