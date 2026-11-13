#version 3.7;
#ifndef (CameraSpan) #declare CameraSpan = 0.1; #end
#ifndef (Shape) #declare Shape = 0; #end
#ifndef (CameraOnly) #declare CameraOnly = 0; #end
#ifndef (ExportMode) #declare ExportMode = 0; #end
#ifndef (BouncePasses) #declare BouncePasses = 0; #end
#ifndef (Power) #declare Power = 0.05; #end
#ifndef (SourceDepth) #declare SourceDepth = -0.35; #end
global_settings {
  assumed_gamma 1 mm_per_unit 1 max_trace_level 8
  subsurface { method 2 samples 16, 32 spacing 0.5 }
}
#if ((BouncePasses > 0 | ExportMode > 0) & !CameraOnly)
camera { orthographic location <-3, 2, 0> look_at <1, 0.7, 0> right z * 0.3 up y * 0.3 }
box { <1, 0.4, -0.4>, <1.05, 1, 0.4> pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#else
camera { orthographic location <0, 3, -0.001> look_at 0 right x * CameraSpan up z * CameraSpan }
#end
background { rgb 0 }
#if (Shape = 0)
box { <-1, -1, -1>, <1, 0, 1>
#else
superellipsoid { <0.3, 0.3> scale <1, 0.5, 1> translate -y * 0.5
#end
  pigment { rgb 0.8 }
  finish { ambient 0 diffuse 1 subsurface { translucency 0.5 } }
  interior { ior 1.4 }
  #if (BouncePasses > 0 | ExportMode > 0)
    light {
      #if (ExportMode = 1) emission #end
      #if (ExportMode = 2) bounce { passes 1 }
      #else #if (BouncePasses > 0) bounce { passes BouncePasses } #end #end
      samples 32
    }
  #end
}
sphere {
  <0, SourceDepth, 0>, 0.12
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness Power samples 16 attenuation { cutoff 0 } } }
}
