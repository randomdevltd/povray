#version 3.7;
global_settings { assumed_gamma 1 }
camera { perspective location <0, 1, -4> look_at <0, 0, 6> right x * image_width / image_height }
background { color rgb <0.4, 0.6, 1> }
mesh2 {
  vertex_vectors { 4, <-2, 0, 0>, <2, 0, 0>, <2, 0, 12>, <-2, 0, 12> }
  uv_vectors { 4, <0, 0>, <1, 0>, <1, 1>, <0, 1> }
  face_indices { 2, <0, 1, 2>, <0, 2, 3> }
  uv_indices { 2, <0, 1, 2>, <0, 2, 3> }
  texture {
    uv_mapping
    pigment { checker color rgb 1 color rgb 0 scale 0.02 }
    finish { ambient 0 diffuse 0 emission 1 }
  }
}
