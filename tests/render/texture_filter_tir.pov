// Texture filter test: tinted glass blocks with plain pigments, whose light reflects totally inside them.
#version 3.8;
global_settings { assumed_gamma 1 max_trace_level 16 }
camera { location <0, 2.2, -6> look_at <0, 0.6, 0> angle 45 right x * image_width / image_height }
light_source { <-4, 8, -5> rgb 1 }
background { rgb <0.55, 0.7, 0.9> }
plane { y, 0 pigment { rgb 0.7 } finish { diffuse 0.8 } }
#declare Glass = material {
  texture { pigment { rgbf <0.55, 0.85, 0.95, 0.6> } finish { diffuse 0.1 reflection 0.05 specular 0.3 } }
  interior { ior 1.31 }
}
#for (I, 0, 3)
  #for (J, 0, 1)
    box { <-0.45, 0, -0.45>, <0.45, 0.9, 0.45> material { Glass } rotate y * (17 * I + 29 * J) translate <-1.5 + I, 0.001 + 0.92 * J, 0> }
  #end
#end
