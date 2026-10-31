// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// The detail of an interior refraction field, the cell width of its prepared grid, against crackle cells 0.01 units across. Three five-unit plates bend a striped card behind them. From the top: detail auto, which the plate's size sets near 0.04, far coarser than the cells, so their index is sampled into a coarse ripple; detail 0.05, coarser still; detail 0.005, which resolves the cells and shatters the stripes into crackle. +w640 +h360
#version 4.0;

global_settings { assumed_gamma 1 }
camera { location <0, 0.9, -4> look_at <0, 0.55, 0> angle 16 right x * image_width / image_height }
light_source { <-1, 4, -3> rgb 1 }

// The card: white with narrow coloured stripes, seen through the plates.
plane {
  z, 0.5
  pigment { gradient x color_map { [0 rgb 1] [0.3 rgb 1] [0.3 rgb <0.75, 0.2, 0.15>] [0.5 rgb <0.75, 0.2, 0.15>] [0.5 rgb <0.15, 0.5, 0.75>] [0.7 rgb <0.15, 0.5, 0.75>] [0.7 rgb 1] [1 rgb 1] } scale 0.3 }
  finish { diffuse 0.85 }
}

#declare S = function(T) { select(T, 0, select(T - 1, T * T * (3 - 2 * T), 1)) }
// The index field fades to zero at each face of a 5 x 0.18 x 0.6-unit plate.
#declare Fade = function(x, y, z) { S((2.5 - abs(x)) / 0.25) * S((0.09 - abs(y)) / 0.02) * S((0.3 - abs(z)) / 0.06) }
#declare Fine = function { pattern { crackle solid scale 0.01 } }
#declare PlateIndex = function { pigment { function { Fine(x, y, z) * Fade(x, y, z) } } }

#macro Plate(Y, Label)
  text { ttf "timrom.ttf" Label 0.001, 0 scale 0.04 pigment { rgb 0.05 } finish { diffuse 0.5 } translate <-0.47, Y - 0.07, -0.32> }
  box {
    <-2.5, -0.09, -0.3>, <2.5, 0.09, 0.3>
    pigment { rgbt 1 }
    interior {
      ior 1
      refraction {
        function { 0.004 * PlateIndex(x, y, z).red }
        #switch (Y) #case (0.58) detail 0.05 #break #case (0.37) detail 0.005 #break #else detail auto #end
      }
    }
    translate y * Y
  }
#end
Plate(0.79, "detail auto")
Plate(0.58, "detail 0.05")
Plate(0.37, "detail 0.005")
