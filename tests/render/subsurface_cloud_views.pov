// Two cameras of one job share their subsurface method 2 clouds; each camera's image matches a render of it alone (Cam 0 or 1,
// without an output block). Order 1 declares the cameras the other way round. Case 0: a closed box, whose cloud lies on its
// surface map and is shared, on a translucent plane, whose cloud each camera spaces by its own pixel; 1 adds radiosity with
// subsurface radiosity, whose cells stay with their view; 2 adds method 2 photons through a glass sphere onto the box.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Case) #declare Case = 0; #end
#ifndef (Order) #declare Order = 0; #end
#declare Own = (Cam = -1);
global_settings {
  assumed_gamma 1 ambient_light 0 mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
  #if (Case = 1) radiosity { count 30 nearest_count 4 recursion_limit 1 pretrace_start 0.08 pretrace_end 0.04 subsurface on } #end
  #if (Case = 2) photons { method 2 quality 0.3 } #end
}

#if (((Cam = -1) & (Order = 0)) | (Cam = 0))
  camera { location <0, 1.5, -4> look_at <0, 0.4, 0> angle 45 #if (Own) output #end }
#end
#if ((Cam = -1) | (Cam = 1))
  camera { location <3, 2.5, 2.5> look_at <0, 0.4, 0> angle 30 #if (Own) output #end }
#end
#if ((Cam = -1) & (Order = 1))
  camera { location <0, 1.5, -4> look_at <0, 0.4, 0> angle 45 #if (Own) output #end }
#end

light_source { <-2, 5, -3> rgb 1.3 }
#declare Skin = material {
  texture { pigment { rgb <0.8, 0.4, 0.25> } finish { ambient 0 diffuse 0.2 subsurface { translucency <3, 2, 1.5> } } }
  interior { ior 1.4 }
}
box { <-0.5, 0, -0.5>, <0.5, 0.8, 0.5> material { Skin } }
plane { y, 0 material { Skin } }
#if (Case = 2)
  sphere { <-0.6, 1.4, -0.8>, 0.3 pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1.5 } photons { target refraction on } }
#end
