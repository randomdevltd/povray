// bounce_brick_wall.pov: a backlit wall of flush translucent bricks in running bond, two deep, as one union with one
// bounce light. Mode 0 translucent bricks with bounce; 1 the same wall as one body (the reference); 2 opaque bricks with
// bounce, which must stay dark in front; 3 translucent bricks without bounce.
#version 3.8;
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Cells) #declare Cells = 65536; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Columns) #declare Columns = 14; #end
#ifndef (Rows) #declare Rows = 12; #end
#ifndef (Layers) #declare Layers = 2; #end
#ifndef (Translucency) #declare Translucency = 4; #end
#ifndef (Power) #declare Power = 200; #end
global_settings { assumed_gamma 1 mm_per_unit 10 subsurface { method 2 samples 16, 4 } }
camera { location <-9, 3, -34> look_at <0, 0, 1> angle 50 }
background { rgb 0 }
light_source { <4, 2, 30>, rgb Power fade_distance 1 fade_power 2 }

#declare Width = 2 * Columns;
#declare Translucent = material {
  texture { pigment { rgb <0.95, 0.95, 1> } finish { ambient 0 emission 0 diffuse 0.9 subsurface { translucency Translucency } } }
  interior { ior 1.3 }
}
#declare Opaque = material { texture { pigment { rgb <0.95, 0.95, 1> } finish { ambient 0 emission 0 diffuse 0.9 } } }
#declare Brick = material { #if (Mode = 2) Opaque #else Translucent #end }

// Each row and layer starts half a brick in where the one below or behind it does not, so no joint runs through.
#macro Bricks()
  #for (L, 0, Layers - 1)
    #for (R, 0, Rows - 1)
      #local Shift = mod(R + L, 2);
      #local X = -Width / 2;
      #if (Shift) box { <X, R - Rows / 2, L>, <X + 1, R + 1 - Rows / 2, L + 1> } #local X = X + 1; #end
      #while (X + 2 <= Width / 2 + 1.0e-9)
        box { <X, R - Rows / 2, L>, <X + 2, R + 1 - Rows / 2, L + 1> }
        #local X = X + 2;
      #end
      #if (X < Width / 2 - 1.0e-9) box { <X, R - Rows / 2, L>, <Width / 2, R + 1 - Rows / 2, L + 1> } #end
    #end
  #end
#end

#if (Mode = 1)
  box { <-Width / 2, -Rows / 2, 0>, <Width / 2, Rows / 2, Layers> material { Brick } }
#else
  union {
    Bricks()
    material { Brick }
    #if (Mode != 3) light { bounce domain_cells Cells samples Samples } #end
  }
#end
