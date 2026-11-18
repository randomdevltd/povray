// A wall of small method 2 bricks at the default density, beside a sphere and a union of bricks with their own budgets.
// Wax diffuses shorter than the density's floor; Flesh's colour and translucency set the sphere's, and Mixed makes the
// union's second brick Flesh.
#version 3.7;
#ifndef (Bricks) #declare Bricks = 200; #end
#ifndef (Mm) #declare Mm = 10; #end
#ifndef (Own) #declare Own = 2000; #end
#ifndef (Joined) #declare Joined = 500; #end
#ifndef (Colour) #declare Colour = <0.9, 0.6, 0.4>; #end
#ifndef (T) #declare T = 0.1; #end
#ifndef (Mixed) #declare Mixed = 0; #end
#ifndef (FleshMode) #declare FleshMode = 0; #end
global_settings { assumed_gamma 1 mm_per_unit Mm subsurface { method 2 samples 1, 1 } }
camera { orthographic location <1.6, 0.6, -3> look_at <1.6, 0.6, 0> right x*4.4 up y*3 }
light_source { <3, 4, -6>, rgb 1 }
#declare Wax = texture { pigment { rgb <0.9, 0.6, 0.4> } finish { ambient 0 diffuse 1 subsurface { translucency 0.1 } } }
#declare Flesh = texture { pigment { rgb Colour } finish { ambient 0 diffuse 1 subsurface { translucency T
  #if (FleshMode = 1) pigment { rgb Colour } #elseif (FleshMode = 2) colour rgb Colour #end
} } }
#declare Brick = box { <0, 0, 0>, <0.12, 0.06, 0.08> texture { Wax } interior { ior 1.4 } }
#declare Columns = 20;
#declare I = 0;
#while (I < Bricks)
  object { Brick translate <mod(I, Columns) * 0.15, div(I, Columns) * 0.09, 0> }
  #declare I = I + 1;
#end
sphere { <3.4, 1.4, 0>, 0.3 texture { Flesh } interior { ior 1.4 } #if (Own > 0) subsurface { domain_cells Own } #end }
union {
  object { Brick translate <3.2, 0.2, 0> }
  object { Brick #if (Mixed) texture { Flesh } #end translate <3.4, 0.2, 0> }
  #if (Joined > 0) subsurface { domain_cells Joined } #end
}
