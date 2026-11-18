// Surface bounce on a thin lens: the subsurface_leaf lens (two radius-LensR spheres intersected, 3 x 1.24, 0.06 thick) as
// an opaque bounce light over a pale ground, lit from above; its bounce reaches the ground under it.
// Cells=N sets the leaf's point budget.
#version 3.8;
#ifdef (Cells) #default { light { domain_cells Cells } } #end
global_settings { assumed_gamma 1 }
camera { location <0.3, 1.6, -5.4> look_at <0, 1.0, 0> angle 40 right x * image_width / image_height }
light_source { <-3, 8, -2> rgb 1.4 }
plane { y, 0 pigment { rgb 0.8 } finish { ambient 0 diffuse 0.9 } }
#declare Mid = 0.06;
#declare LensR = (1 + (Mid / 2) * (Mid / 2)) / Mid;
intersection {
  sphere { <0, LensR - Mid / 2, 0>, LensR }
  sphere { <0, -(LensR - Mid / 2), 0>, LensR }
  scale <1.5, 1, 0.62>
  rotate <-80, 18, -14>
  translate <0, 1.6, 0>
  pigment { rgb <0.9, 0.5, 0.2> } finish { ambient 0 diffuse 0.9 }
  light { bounce }
}
