// domain_cells.pov: an interior light whose source domain budget comes from its own domain_cells item or from the
// scene-wide #default light domain_cells, never from its medium's; the .sh names the cases.
#version 3.8;
#ifndef (Cells) #declare Cells = 0; #end
#ifndef (LightCells) #declare LightCells = 0; #end
#ifndef (MediumCells) #declare MediumCells = 0; #end
#ifndef (DefaultLightCells) #declare DefaultLightCells = 0; #end
#if (Cells > 0) #default { media { domain_cells Cells } } #end
#if (DefaultLightCells > 0) #default { light { domain_cells DefaultLightCells } } #end
global_settings { assumed_gamma 1 }
camera { location <0, 2.5, -4> look_at <0, 0.3, 0> right x up y }
background { rgb 0 }
plane { y, -1 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere {
  0, 0.6 hollow
  pigment { rgbt 1 }
  interior {
    light { brightness 12 samples 8 #if (LightCells > 0) domain_cells LightCells #end }
    media { method 4 #if (MediumCells > 0) domain_cells MediumCells #end absorption 0.5 }
  }
}
