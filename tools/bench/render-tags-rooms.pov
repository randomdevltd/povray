// Two closed rooms 100 units apart, joined by a one-way portal; Filtered=1 gives each room its own prepared set.
#version 4.0;
#ifndef (Filtered) #declare Filtered = 0; #end
#ifndef (Photons) #declare Photons = 0; #end
global_settings { assumed_gamma 1 #if (Photons) photons { spacing 0.01 } #end }

#macro Tags(Name) #if (Filtered) tags { Name } #end #end

camera {
  location <0, 2, -4.5>
  look_at <0, 1.6, 5>
  angle 70
  #if (Filtered) filter_tags { "a" } #end
}

#macro Room(Name, Offset, Seed)
  difference {
    box { <-5.2, -0.2, -5.2>, <5.2, 4.2, 5.2> }
    box { <-5, 0, -5>, <5, 4, 5> }
    pigment { rgb 0.7 }
    translate Offset
    Tags(Name)
  }
  #local R = seed(Seed);
  #for (I, 0, 149)
    sphere {
      <rand(R) * 9 - 4.5, 0.2, rand(R) * 9 - 4.5>, 0.2
      pigment { rgb <rand(R), rand(R), rand(R)> }
      finish { specular 0.4 }
      translate Offset
      Tags(Name)
    }
  #end
  #for (I, 0, 5)
    light_source { <cos(I * pi / 3) * 3, 3.6, sin(I * pi / 3) * 3> + Offset, rgb 0.25 Tags(Name) }
  #end
  #if (Photons)
    sphere {
      <0, 1, 1>, 0.8
      pigment { rgbf 1 }
      interior { ior 1.5 }
      photons { target refraction on reflection on }
      translate Offset
      Tags(Name)
    }
  #end
#end

Room("a", <0, 0, 0>, 1)
Room("b", <100, 0, 0>, 2)

portal {
  box { <-1.2, 0.4, 4.7>, <1.2, 2.8, 4.8> }
  to { translate <100, 0, -9.4> }
  far off
  no_lights
  #if (Filtered) filter_tags { "b" } #end
  Tags("a")
}
