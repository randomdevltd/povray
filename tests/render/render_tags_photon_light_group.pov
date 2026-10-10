#version 4.0;
#ifndef (Version) #declare Version = 4.0; #end
#version Version;
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Grouped) #declare Grouped = 1; #end
#ifndef (GroupPhotons) #declare GroupPhotons = -1; #end
#ifndef (PhotonOnly) #declare PhotonOnly = 1; #end
#ifndef (SourceRefraction) #declare SourceRefraction = 1; #end
#macro KeepTag() #if (Version >= 4) tags { "keep" } #end #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 10
  photons { method 2 quality 0.125 }
}
camera {
  orthographic location <1.5, 3, -6> look_at <1.5, 0.5, 0> right 5*x up 2.5*y
  #if (Version >= 4) filter_tags { "keep" } #end
}
#declare Lens = sphere {
  <0, 0.75, 0>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1.5 } photons { target refraction on reflection off collect off }
}
#declare Receiver = box {
  <-1, -0.1, -1>, <1, 0, 1> pigment { rgb 1 } finish { ambient 0 diffuse 1 }
}
#if (Grouped) #declare LampGroup = light_group { #end
  light_source { <1.5, 4, 0> rgb 3 photon_only PhotonOnly photons { refraction SourceRefraction reflection off } KeepTag() }
  object { Lens KeepTag() }
  object { Receiver KeepTag() }
#if (Grouped)
  global_lights off
  KeepTag()
  #if (GroupPhotons >= 0) photons GroupPhotons #end
}
object { LampGroup }
#end
#if (!Reference)
  object { Lens translate 3*x KeepTag() }
  object { Receiver translate 3*x KeepTag() }
#end
