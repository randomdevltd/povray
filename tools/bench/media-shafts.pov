// Shafts from an area sun through a slatted roof into scattering media; see doc/PERF.md.
#version 3.7;
#ifndef (Samples) #declare Samples = 40; #end
#ifndef (MaxSamples) #declare MaxSamples = Samples; #end
#ifndef (Method) #declare Method = 3; #end
global_settings { assumed_gamma 1.0 }

camera { perspective location <0, 2, -14> look_at <0, 4, 0> angle 65 right x * 4 / 3 }

light_source {
  <-300, 400, 200>, rgb 3
  parallel point_at <0, 0, 0>
  area_light <8, 0, 0>, <0, 0, 8>, 9, 9
  adaptive 1 jitter circular orient
}

plane { y, 0 pigment { rgb 0.6 } }

#for (I, -12, 12)
  box { <I - 0.3, 9, -16>, <I + 0.3, 9.4, 16> pigment { rgb 0.3 } }
#end
#for (I, -3, 3)
  cylinder { <I * 3, 0, 3>, <I * 3, 9, 3>, 0.4 pigment { rgb <0.4, 0.3, 0.2> } }
#end

box {
  <-24, -1, -24>, <24, 12, 24>
  hollow
  pigment { rgbt 1 }
  interior {
    media {
      scattering { 5, 0.05 eccentricity 0.4 extinction 0.65 }
      density { spherical color_map { [0 rgb 1] [1 rgb 0.2] } scale 30 }
      method Method
      intervals 1
      #if (Method = 3) samples Samples #else samples Samples, MaxSamples #end
      aa_level 3
      aa_threshold 0.1
    }
  }
}
