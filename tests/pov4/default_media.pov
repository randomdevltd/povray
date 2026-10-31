// default_media: classic twin of default_media.pov4
#version 4.0;
#default { media { mix replace } interior { light { } } }
global_settings { assumed_gamma 1.0 }
camera { location <0, 2, -4> look_at <0, 0, 4> }
plane { y, 0 pigment { color rgb 0.8 } finish { ambient 0 diffuse 1 } }
sphere { <0, 1, 0>, 0.25 pigment { rgbt 1 } no_image interior { light { pigment { rgb 50 } samples 4 } } }
