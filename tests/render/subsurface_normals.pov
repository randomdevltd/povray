#version 3.7;
#ifndef (Bump) #declare Bump = 1; #end
#ifndef (Strength) #declare Strength = 0.25; #end
#ifndef (Mapped) #declare Mapped = 0; #end
#ifndef (Spacing) #declare Spacing = 0.5; #end

global_settings {
  assumed_gamma 1 mm_per_unit 10
  subsurface { method 2 samples 12, 4 spacing Spacing }
}
camera { orthographic location <0,0,-5> look_at 0 right x*2.7 up y*2.7 }
light_source { <-4,3,-2> rgb 1.5 }
background { rgb 0.02 }
sphere {
  0, 1
  texture {
    #if (Mapped) uv_mapping #end
    pigment { rgb <0.8,0.5,0.35> }
    #if (Bump) normal { wrinkles Strength scale 0.15 } #end
    finish { ambient 0 diffuse 0.7 subsurface { translucency <1.5,0.9,0.5> } }
  }
  interior { ior 1.4 }
}
