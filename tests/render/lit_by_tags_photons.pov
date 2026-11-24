#version 4.0;
// A lamp whose photons only the objects that lit_by_tags it may take: the lens and receiver beside it are lit by it, the
// copies 3 units along x are not, so its photons neither aim at the far lens nor land on the far receiver.
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Restricted) #declare Restricted = 1; #end
#ifndef (OutsideLens) #declare OutsideLens = 1; #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 10
  photons { method 2 quality 0.125 }
}
camera {
  orthographic location <1.5, 3, -6> look_at <1.5, 0.5, 0> right 5*x up 2.5*y
  filter_tags { "keep" }
}
#declare Lens = sphere {
  <0, 0.75, 0>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1.5 } photons { target refraction on reflection off collect off }
}
#declare Receiver = box {
  <-1, -0.1, -1>, <1, 0, 1> pigment { rgb 1 } finish { ambient 0 diffuse 1 }
}
light_source { <1.5, 4, 0> rgb 3 photon_only fade_power 2 fade_distance 0 photons { refraction on reflection off } tags { "keep", "lamp" } }
union {
  object { Lens }
  object { Receiver }
  tags { "keep" }
  #if (Restricted) lit_by_tags { "lamp" } #end
}
// no_image: the reference has no outside lens to refract the camera onto the lit receiver
#if (!Reference)
  union {
    #if (OutsideLens) object { Lens no_image translate 3*x } #end
    object { Receiver translate 3*x }
    tags { "keep" }
    #if (Restricted) lit_by_tags { !"lamp" } #end
  }
#end
