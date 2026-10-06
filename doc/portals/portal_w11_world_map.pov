// A live Mercator map on a curled skein page beside the procedural globe sampled by its screen camera.
#version 4.0;
global_settings { assumed_gamma 1.0 }
#include "projections.inc"
#ifndef (BenchmarkFlatPage) #declare BenchmarkFlatPage = 0; #end

#declare G = <1.65, 0.9, 0.35>;
#declare MapG = <500, 0.9, 0.35>;
#declare WorldDetail = pigment {
  bozo turbulence 0.82 octaves 8 lambda 2.35 omega 0.58
  colour_map {
    [0.00 rgb <0.025, 0.12, 0.35>]
    [0.40 rgb <0.035, 0.2, 0.58>]
    [0.46 rgb <0.08, 0.42, 0.7>]
    [0.475 rgb <0.78, 0.7, 0.38>]
    [0.49 rgb <0.12, 0.5, 0.16>]
    [0.64 rgb <0.32, 0.42, 0.12>]
    [0.76 rgb <0.4, 0.27, 0.12>]
    [0.87 rgb <0.56, 0.5, 0.34>]
    [0.92 rgb 0.88]
    [1.00 rgb 1]
  }
  scale 0.055
};
#declare PolarIce = pigment {
  granite turbulence 0.55 octaves 6
  colour_map { [0 rgb <0.55, 0.78, 0.92>] [0.45 rgb <0.88, 0.96, 1>] [1 rgb 1] }
  scale 0.035
};
#declare World = pigment {
  function { abs(y - 0.9) / 0.7 }
  pigment_map {
    [0.72 WorldDetail]
    [0.78 WorldDetail]
    [0.84 PolarIce]
    [1.00 PolarIce]
  }
};
#declare MapCamera = CylindricalProjectionCamera(ProjectionMercator, -75, 75, ProjectionSphereOrigin, 0.78, 1, MapG, z, x, y);
#if (!BenchmarkFlatPage)
  #declare Page = skein_mesh {
    expressions {
      translate <-0.5, -0.5, 0>
      scale <3.5, 1.75, 1>
      crease { axis path { <1.35, -1, 0>, <1.35, 1, 0> } radius 0.22 angle 65 }
      envelope { thickness function(uv) { 0.025 * pow(sin(pi * uv.v), 0.25) } edge round }
    }
    closed u
    ends sealed
  };
#end

camera { location <-0.4, 4.5, -7.5> look_at <-0.4, 0.5, 0.1> angle 50 }
background { rgb <0.055, 0.07, 0.11> }
light_source { <-4, 8, -5> rgb 1.45 }
light_source { <5, 5, -2> rgb <0.3, 0.38, 0.5> }
box { <-6, -0.2, -5>, <6, 0, 5> pigment { wood colour_map { [0 rgb <0.18, 0.07, 0.025>] [1 rgb <0.52, 0.25, 0.08>] } rotate 90 * x scale 0.35 } finish { specular 0.25 } }

sphere {
  G, 0.7
  pigment { World }
  finish { diffuse 0.75 specular 0.35 roughness 0.015 }
}
sphere { MapG, 0.7 pigment { World } finish { emission 1 diffuse 0 } }
torus { 0.76, 0.035 rotate 90 * x translate G pigment { rgb <0.8, 0.58, 0.18> } finish { metallic specular 0.5 } }
cylinder { G - 0.8 * y, G - 0.68 * y, 0.05 pigment { rgb <0.55, 0.3, 0.08> } }
cone { G - 0.8 * y, 0.38, G - 0.92 * y, 0.48 pigment { rgb <0.3, 0.14, 0.04> } }

#if (BenchmarkFlatPage)
  box {
    <-1.75, -0.875, -0.025>, <1.75, 0.875, 0.025>
    pigment { screen { camera { MapCamera } } scale <3.5, 1.75, 1> translate <-1.75, -0.875, 0> }
    finish { emission 1 diffuse 0 }
    rotate 90 * x
    translate <-1.35, 0.055, -0.65>
  }
#else
  object {
    Page
    texture {
      uv_mapping
      pigment { screen { camera { MapCamera } } }
      finish { diffuse 0.8 specular 0.08 }
    }
    rotate 90 * x
    translate <-1.35, 0.055, -0.65>
  }
#end
