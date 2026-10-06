#version 4.0;
global_settings { assumed_gamma 1 }
#declare View = camera { location <100, 0.5, -3> look_at <100, 0.5, 0> right x up y };
camera { location <0.5, 0.5, -3> look_at <0.5, 0.5, 0> right x up y }
box {
  <98, -1, 0>, <102, 2, 0.01>
  pigment { checker color rgb 1 color rgb 0 scale 0.02 }
  finish { ambient 0 diffuse 0 emission 1 }
}
box {
  <0, 0, 0>, <1, 1, 0.01>
  pigment { screen { camera { View } } }
  finish { ambient 0 diffuse 0 emission 1 }
}
