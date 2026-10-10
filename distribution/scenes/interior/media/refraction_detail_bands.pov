// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A 500 mm plate of clear material whose index rises across 2 mm seams every 10 mm, like the cell walls
// of a mineral. The camera looks through it at a striped card. With refraction_detail 1 every seam bends
// the card's image by its own share; Declare=Detail=0 leaves the setting out, and the seams that fall
// between the marcher's steps dither into speckle instead. +w640 +h360
#version 4.0;

#ifndef (Detail) #declare Detail = 1; #end

global_settings { assumed_gamma 1 mm_per_unit 100 #if (Detail > 0) refraction_detail Detail #end }
camera { location <0, 0.9, -4> look_at <0, 0.55, 0> angle 16 right x * image_width / image_height }
light_source { <-1, 4, -3> rgb 1 }

// The card: white with coloured stripes, seen through the plate.
plane {
  z, 0.5
  pigment { gradient x color_map { [0 rgb 1] [0.44 rgb 1] [0.46 rgb <0.75, 0.2, 0.15>] [0.54 rgb <0.15, 0.5, 0.75>] [0.56 rgb 1] [1 rgb 1] } scale 1.2 translate -0.6 * x }
  finish { diffuse 0.85 }
}

#declare S = function(T) { select(T, 0, select(T - 1, T * T * (3 - 2 * T), 1)) }
// The plate: 500 x 350 x 60 mm, its density fading to nothing at each face so the surfaces do not refract.
#declare Fade = function(x, y, z) { S((2.5 - abs(x)) / 0.25) * S((1.75 - abs(y)) / 0.25) * S((0.3 - abs(z)) / 0.06) }
#declare U = function(x) { x - 0.1 * floor((x + 0.01) / 0.1) }
// 2 mm seams rising 0 to 1 across each cell wall, every 10 mm.
#declare Seam = function(x) { S(U(x) / 0.02) }

box {
  <-2.5, -1.75, -0.3>, <2.5, 1.75, 0.3>
  pigment { rgbt 1 }
  interior {
    ior 1
    media { method 3 refraction 0.0004 density { function { Seam(x) * Fade(x, y, z) } } }
  }
}
