#version 3.8;
// A method 2 body lit only by a volume light buried in it: a sphere lamp in a sphere (Vein=0), or a thin vein lamp just under
// the top of a slab (Vein=1), seen from View degrees off the top's normal. Influence 1e9 sends every hit to the estimate path,
// which walks the per-pixel internal paths; the default reads the buried light's scattered part from the point cloud.
#ifndef (Influence) #declare Influence = 0.003; #end
#ifndef (Samples) #declare Samples = 4; #end
#ifndef (Scale) #declare Scale = 1; #end
#ifndef (Passes) #declare Passes = 0; #end
#ifndef (Cells) #declare Cells = 4096; #end
#ifndef (Vein) #declare Vein = 0; #end
#ifndef (View) #declare View = 0; #end
global_settings {
  assumed_gamma 1 mm_per_unit 1 / Scale max_trace_level 8
  subsurface { method 2 samples 4, 2 }
}
background { rgb 0 }
#declare Flesh = material {
  texture { pigment { rgb 0.9 } finish { ambient 0 diffuse 1 subsurface { translucency 0.3 minimum_influence Influence } } }
  interior { ior 1.3 }
}
#if (Vein)
camera { orthographic location <0, 4, 0> look_at 0 right x * 2 up z * 2 rotate z * View scale Scale }
box { <-2, -1, -2>, <2, 0, 2> material { Flesh } subsurface { domain_cells Cells } scale Scale }
cylinder { <0, -0.15, -3>, <0, -0.15, 3>, 0.04
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness 1 samples Samples attenuation { cutoff 0 } } }
  scale Scale
}
#else
camera { location <0, 0, -4> look_at 0 angle 30 scale Scale }
sphere { 0, 1 material { Flesh } subsurface { domain_cells Cells } scale Scale }
sphere { <0, 0, 0>, 0.3
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness 0.2 samples Samples attenuation { cutoff 0 } } }
  scale Scale
}
#if (Passes > 0)
// a passive volume beside it that scatters on what it receives, in Passes bounce passes
sphere { <0.35, 0.3, -0.2>, 0.25
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior {
    light { brightness 1 samples Samples attenuation { cutoff 0 } bounce { passes Passes } }
    media { method 4 scattering { 1, 20 } }
  }
  scale Scale
}
#end
#end
