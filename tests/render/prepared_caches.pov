// prepared_caches.pov: a lamp of glowing gas whose colour varies (an interior light lattice), a haze of varying density
// that scatters the light it receives once more (a media density grid and a volume bounce field) and walls that bounce
// what reaches them (surface bounce fields), for saving prepared caches and loading them on a later render. Cells sets
// the lattices' and grids' cells, Nodes the walls' bounce nodes; Tint changes the lamp's colour, and Inputs or ByWidth
// take it from input files or the image width.
#version 3.8;
#ifndef (Cells) #declare Cells = 4000; #end
#ifndef (Nodes) #declare Nodes = 512; #end
#ifndef (Tint) #declare Tint = 0.6; #end
// Inputs reads the tint from two files in turn, the later winning; Rewrite, set by them, then edits the first mid-parse
#ifdef (Inputs)
  #include "prepared_caches_a.inc"
  #include "prepared_caches_b.inc"
  #ifdef (Rewrite)
    #fopen Edit "prepared_caches_a.inc" write
    #write (Edit, "#declare Tint = 0.9;\n")
    #fclose Edit
  #end
#end
// ByWidth derives the tint from the image width, read through the global dictionary
#ifdef (ByWidth) #declare Tint = global.image_width / 80; #end
global_settings { assumed_gamma 1 }
camera { location <0, 1.2, -3.2> look_at <0, 0.8, 0> angle 62 }
background { rgb 0 }
light_source { <1.1, 2.2, -1.2>, rgb 1.5 fade_distance 1 fade_power 2 }

#macro Wall(Low, High, Colour)
  box { Low, High
    pigment { rgb Colour } finish { ambient 0 emission 0 diffuse 0.8 }
    light { bounce { passes 2 } samples 8 domain_cells Nodes }
  }
#end
Wall(<-1.6, -0.1, -1.2>, <1.6, 0, 1.6>, <0.8, 0.75, 0.7>)
Wall(<-1.6, 0, 1.5>, <1.6, 2.4, 1.6>, <0.7, 0.75, 0.8>)
Wall(<-1.7, 0, -1.2>, <-1.6, 2.4, 1.6>, <0.85, 0.45, 0.35>)

// the lamp: its gas glows in a varying colour
sphere { <-0.55, 0.85, 0.35>, 0.5 pigment { rgbt 1 } hollow
  interior {
    media { method 4 domain_cells Cells scattering { 1, rgb 0.5 } absorption rgb 0.1
      density { spherical color_map { [0 rgb 0] [1 rgb 1] } scale 0.5 translate <-0.55, 0.85, 0.35> } }
    light {
      pigment { bozo color_map { [0 rgb <1, Tint, 0.3>] [1 rgb <1, 0.9, 0.7>] } scale 0.15 }
      brightness 25 lumens samples 16 domain_cells Cells
    }
  }
}

// the haze: a box of drifting density lit by both lights, which scatters what it receives once more
box { <0.05, 0, -0.5>, <1.3, 1.7, 0.9> pigment { rgbt 1 } hollow
  interior {
    media { method 4 domain_cells Cells scattering { 1, rgb 0.35 } absorption rgb 0.05
      density { bozo color_map { [0 rgb 0] [1 rgb 1] } scale 0.25 } }
    light { bounce { passes 1 } samples 8 domain_cells Cells }
  }
}
