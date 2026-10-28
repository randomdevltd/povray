// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A five-unit plate whose index rises across 0.02-unit seams every 0.1 units, like mineral cell walls.
// Declare=Detail sets the index detail in scene units; its default is 0.01. Detail=0 selects auto.
// A striped card behind the plate shows the bending. +w640 +h360
#version 4.0;

#ifndef (Detail) #declare Detail = 0.01; #end

global_settings { assumed_gamma 1 }
camera { location <0, 0.9, -4> look_at <0, 0.55, 0> angle 16 right x * image_width / image_height }
light_source { <-1, 4, -3> rgb 1 }

// The card: white with coloured stripes, seen through the plate.
plane {
  z, 0.5
  pigment { gradient x color_map { [0 rgb 1] [0.44 rgb 1] [0.46 rgb <0.75, 0.2, 0.15>] [0.54 rgb <0.15, 0.5, 0.75>] [0.56 rgb 1] [1 rgb 1] } scale 1.2 translate -0.6 * x }
  finish { diffuse 0.85 }
}

#declare S = function(T) { select(T, 0, select(T - 1, T * T * (3 - 2 * T), 1)) }
// The index field fades to zero at each face of the 5 x 3.5 x 0.6-unit plate.
#declare Fade = function(x, y, z) { S((2.5 - abs(x)) / 0.25) * S((1.75 - abs(y)) / 0.25) * S((0.3 - abs(z)) / 0.06) }
#declare U = function(x) { x - 0.1 * floor((x + 0.01) / 0.1) }
// The field rises from zero to one across each 0.02-unit seam, every 0.1 units.
#declare Seam = function(x) { S(U(x) / 0.02) }

box {
  <-2.5, -1.75, -0.3>, <2.5, 1.75, 0.3>
  pigment { rgbt 1 }
  interior {
    ior 1
    refraction {
      strength 0.0004
      field { pigment { function { Seam(x) * Fade(x, y, z) } } }
      #if (Detail > 0) detail Detail #else detail auto #end
    }
  }
}
