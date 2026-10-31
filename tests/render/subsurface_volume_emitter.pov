#version 3.8;
// Zero fade_distance selects exact inverse-square attenuation from version 3.8.
#ifndef (PointSource) #declare PointSource = 0; #end
#ifndef (NearSource) #declare NearSource = 0; #end
#ifndef (Photons) #declare Photons = 0; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 4; #end
#ifndef (Samples) #declare Samples = 256; #end
#ifndef (SingleSamples) #declare SingleSamples = 32; #end
#ifndef (ReuseViews) #declare ReuseViews = 0; #end
#ifndef (CloudSpacing) #declare CloudSpacing = 0.3; #end
#declare Height = (NearSource ? 1.2 : 20);
#declare Radius = (NearSource ? 0.3 : 0.02);
#declare Intensity = pi * 0.15 * Height * Height;
global_settings {
  assumed_gamma 1 max_trace_level 8 mm_per_unit 1
  subsurface { method 2 samples Samples, SingleSamples spacing CloudSpacing }
  #if (Photons) photons { method 2 quality PhotonQuality } #end
}
#for (View, 0, ReuseViews)
  camera { orthographic location <0, 5, -0.001> look_at 0 right x * 1 up z * 1
    #if (ReuseViews) output #end
  }
#end
background { rgb 0 }
box {
  <-3, -3, -3>, <3, 0, 3>
  pigment { rgb 0.8 }
  finish { ambient 0 diffuse 1 subsurface { translucency 0.2 } }
  interior { ior 1.4 }
  photons { pass_through off }
}
box {
  <-20, 0.6, -20>, <20, 0.7, 20>
  pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1 }
  photons { target 1 refraction on reflection off collect off }
}
#if (PointSource)
  light_source {
    <0, Height, 0> rgb Intensity / pi
    fade_power 2 fade_distance 0
    photons { refraction on reflection off }
  }
#else
  sphere {
    <0, Height, 0>, Radius
    hollow no_image pigment { rgbt 1 }
    interior { light {
      brightness (4 * pi * Intensity) lumens samples 64
      photons { refraction on reflection off }
    } }
  }
#end
