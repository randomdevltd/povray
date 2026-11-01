// prepared_source_cells.pov: an interior light whose source domain budget comes from global_settings; the .sh names the cases.
#version 3.8;
#ifndef (Cells) #declare Cells = 0; #end
global_settings { assumed_gamma 1 #if (Cells > 0) prepared_source_cells Cells #end }
camera { location <0, 2.5, -4> look_at <0, 0.3, 0> right x up y }
background { rgb 0 }
plane { y, -1 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere {
  0, 0.6 hollow
  pigment { rgbt 1 }
  interior {
    light { brightness 12 samples 8 }
    media { method 4 resolution 0.25 absorption 0.5 }
  }
}
