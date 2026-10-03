// skein_page.pov: a page from a declared sheet group, its corner curled toward the reader, each turn of the roll wider than the last.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Sheet = expressions { translate <-0.5, 0, 0>  scale <0.85, 1.1, 1> }
#declare Page = skein {
  expressions {
    Sheet
    curl { pivot path { <0.3, 0.6, -0.05>, <-0.7, 1.6, -0.05> }  travel path { 0, <0, 0, 0>, 2, <0, 0, -0.06> } }
  }
}

background { rgb <0.32, 0.36, 0.4> }
light_source { <-2, 3, -4> rgb 1 }
light_source { <3, 1, -2> rgb 0.3 }
camera { location <0.5, 0.9, -1.9> look_at <0.05, 0.55, 0> angle 45 }
object {
  Page
  texture {
    uv_mapping
    pigment { gradient y  color_map { [0 rgb 0.96] [0.92 rgb 0.96] [0.92 rgb <0.55, 0.65, 0.85>] [1 rgb <0.55, 0.65, 0.85>] } scale 1/24 }
    finish { diffuse 0.9 }
  }
  double_illuminate
}
