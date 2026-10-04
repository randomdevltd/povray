#ifndef (Method) #declare Method = 0; #end

global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface {
    samples 12, 4
    spacing 0.5
    #if (Method > 0) method Method #end
  }
}

camera { location <0, 1.5, -5> look_at <0, 0.5, 0> right x*4/3 angle 45 }
light_source { <-2, 5, -3> rgb 1.3 area_light <0.5, 0, 0>, <0, 0, 0.5>, 2, 2 }
plane { y, -0.1 pigment { rgb <0.3, 0.3, 0.35> } finish { diffuse 0.8 } }

sphere {
  <0, 0.7, 0>, 0.8
  pigment { rgb <0.8, 0.3, 0.2> }
  finish { ambient 0 diffuse 0.2 subsurface { translucency <0.5, 0.7, 0.9> } }
  interior { ior 1.4 }
}
