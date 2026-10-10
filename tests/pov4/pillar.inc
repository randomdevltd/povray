#declare PillarRadius = 0.25;
#macro Pillar(H, C)
  cylinder { 0, y * H, PillarRadius pigment { color C } translate <-2, 0, 2> }
#end
