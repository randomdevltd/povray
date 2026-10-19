// A glass ball focuses a caustic onto a translucent slab with subsurface method 2; Photons picks none (0), classic (1) or
// method 2 (2). Case 0 one slab; 1 two halves, the right one photons { collect off }; 2 lamp, ball and slab in one light group.
#version 3.8;
#ifndef (Photons) #declare Photons = 2; #end
#ifndef (PhotonOnly) #declare PhotonOnly = 1; #end
#ifndef (Case) #declare Case = 0; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (SubsurfaceMemory) #declare SubsurfaceMemory = 64; #end
#ifndef (PhotonMemory) #declare PhotonMemory = 2; #end
#ifndef (CloseUp) #declare CloseUp = 0; #end
global_settings {
  assumed_gamma 1
  max_trace_level 8
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 spacing Spacing memory SubsurfaceMemory }
  #switch (Photons)
    #case (1) photons { count 200000 } #break
    #case (2) photons { method 2 quality 0.25 memory PhotonMemory } #break
  #end
}

camera {
  #if (CloseUp) location <-0.16, 0.6, -1.6> look_at <-0.16, 0, 0.28> angle 30
  #else location <0, 2.6, -2.6> look_at <0, 0.2, 0> angle 45 #end
  right x * image_width / image_height
}

#declare Lamp = light_source { <0.8, 6, -1.4> rgb 1.5 fade_power 2 fade_distance 4 photon_only PhotonOnly photons { refraction on } }
#declare Ball = sphere {
  <0, 1, 0>, 0.5
  pigment { rgbt 1 } finish { ambient 0 diffuse 0 reflection { 0, 1 fresnel } conserve_energy } interior { ior 1.5 }
  photons { target refraction on reflection on }
}
#declare Skin = material {
  texture { pigment { rgb <0.85, 0.6, 0.45> } finish { ambient 0 diffuse 0.3 subsurface { translucency <1.5, 0.9, 0.6> } } }
  interior { ior 1.4 }
}
#declare Slab = merge {
  box { <-1.5, -0.4, -1.5>, <0.3, 0, 1.5> }
  box { <0.3, -0.4, -1.5>, <1.5, 0, 1.5> #if (Case = 1) photons { collect off } #end }
  material { Skin }
}

#if (Case = 2)
  light_group { light_source { Lamp } object { Ball } object { Slab } global_lights off }
#else
  light_source { Lamp }
  object { Ball }
  object { Slab }
#end
