// framing_tagged: classic twin of framing_tagged.pov4; Reference=1 is the explicit camera it frames to
#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1.0 refraction_detail 1 }
light_source { <4, 6, -3> rgb 1 }
plane { y, -1 pigment { checker color rgb 0.8 color rgb <0.25, 0.3, 0.4> } }
#for (I, -1, 1)
  sphere { x * I * 0.75, 0.25 pigment { color rgb 0.9 } tags { "subject" } }
#end
torus { 0.55, 0.18 translate <1, 1.8, 0> pigment { color rgb <0.95, 0.8, 0.2> } tags { "subject-draft" } }
#declare Draft = concat("subject", "-draft");
#declare Row = tagged { "subject*" & !Draft }
#if (Reference) camera { location <0, 0, -10> direction 5.85 * z right x * 4 / 3 up y look_at 0 }
#else camera { location <0, 0, -10> right x * 4 / 3 up y look_at Row } #end
#declare tagged = 1;
object { Row translate y * 0.8 scale tagged }
