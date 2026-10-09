#version Version;
// Media refraction checks for media_refraction.sh: Declare=Version=<v> Declare=Case=<n>, Declare=Ramp=1 for a vertical ramp backdrop.
#ifndef (Case) #declare Case = 0; #end
#ifndef (Ramp) #declare Ramp = 0; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 0, -10> look_at 0 right x * 8 up y * 6 }
#declare Backdrop = plane {
  z, 4
  #if (Ramp) pigment { gradient y color_map { [0 rgb 0] [1 rgb 1] } scale 8 translate -4 * y }
  #else pigment { checker rgb 0.1 rgb 0.9 scale 0.5 }
  #end
  finish { ambient 0 emission 1 diffuse 0 }
}
#if (Case != 16) object { Backdrop } #end

#declare Clear = texture { pigment { rgbt 1 } finish { diffuse 0 } };
#declare Lens = media { method 3 refraction 0.6 density { spherical scale 2.5 } }
#declare Rise = media { method 3 refraction 0.2 density { gradient y scale 4 translate -2 * y } }
#declare Riser = box { <-5, -2, -2>, <5, 2, 2> texture { Clear } interior { ior 1 media { Rise } } }
#declare Tube = box { <-1, -1, -3>, <1, 1, 3> texture { Clear } };
#declare Ball = sphere { 0, 1.5 texture { Clear } interior { ior 1.5 } };
#declare Water = box { -3, 3 texture { Clear } interior { ior 1.33 } };

#switch (Case)
#case (1)
  sphere { 0, 2 texture { Clear } interior { ior 1 media { refraction 0.5 } media { absorption 0.2 emission 0.1 } } }
#break
#case (2)
  sphere { 0, 2 texture { Clear } interior { ior 1.5 media { absorption 0.2 emission 0.1 / 2.25 } } }
#break
#case (3) // the lens's density fades to zero inside its box, which has a Fresnel mirror finish
  box { -3, 3 texture { pigment { rgbt 1 } finish { diffuse 0 reflection { 0, 1 fresnel } } } interior { ior 1 media { Lens } } }
#break
#case (5)
  object { Riser }
#break
#case (6)
  box { <-5, -2, -2>, <5, 2, 2> texture { Clear }
        interior { ior 1 media { refraction 0.2 density { gradient y scale 4 translate -2 * y } } } }
#break
#case (7)
  object { Riser }
  object { Tube interior { media { mix subtract refraction 0.2 } } }
#break
#case (8)
  object { Riser }
  object { Tube interior { media { mix multiply refraction 1 density { rgb 0 } } } }
#break
#case (9)
  object { Riser }
  object { Tube interior { media { mix replace refraction 0.1 } } }
#break
#case (10)
  object { Tube interior { media { mix subtract refraction 0.2 } } }
  object { Riser }
#break
#case (11) object { Ball } object { Water } #break
#case (12) object { Water } object { Ball } #break
#case (13) object { Ball } #break
#case (14)
  box { <-3, -2, -1>, <3, 2, 1> texture { Clear } interior { media { refraction 0.3 } } }
#break
#case (15) object { Water } #break
#case (16) object { Ball } object { Backdrop } #break
#case (17) object { Ball } box { -3, 3 texture { Clear } interior { ior 1 } } #break
#case (18) sphere { 0, 2.5 texture { Clear } interior { ior 1.5 } } sphere { 0, 1 texture { Clear } interior { ior 1 } } #break
#case (19) box { <-5, -4, -12>, <5, 4, 3.5> texture { Clear } interior { ior 1.5 } } sphere { 0, 1.5 texture { Clear } interior { ior 1 } } #break
#end
