#for (R, 0, dimension_size(Grid, 1) - 1)
  #for (C, 0, dimension_size(Grid, 2) - 1)
    sphere { <C * 0.6 - 0.6, Grid[R][C], R * 0.6>, 0.2 pigment { rgb <R / 2, C / 2, 1> } }
  #end
#end
