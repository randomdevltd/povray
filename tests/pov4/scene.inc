global_settings { assumed_gamma 1 }
camera { location <0, 3, -9> look_at <0, 0.5, 0> right x * 4 / 3 }
light_source { <-4, 8, -6> rgb 1 }
plane { y, -1 pigment { checker rgb 1, rgb 0.3 } }
