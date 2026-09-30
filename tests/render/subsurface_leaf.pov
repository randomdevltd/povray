// A thin leaf lit from behind: an isosurface solid, its flesh a field of voronoi cells (chlorophyll inside, pale walls
// between) distorted over several octaves, read in a thin slice below the surface by volume_sampling. Sky, clouds and
// grass give it something to be seen against. Units: 1 unit = 20 mm, so the leaf is 6 cm long and 1 mm thick.
#version 3.7;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Samples) #declare Samples = 64; #end
#ifndef (Single) #declare Single = 0; #end
#ifndef (Depth) #declare Depth = 0.2; #end
#ifndef (Spread) #declare Spread = 0.06; #end
#ifndef (Mfp) #declare Mfp = 0.25; #end
#ifndef (CellScale) #declare CellScale = 0.16; #end
#ifndef (Warp) #declare Warp = 0.12; #end
#ifndef (Octaves) #declare Octaves = 4; #end
global_settings {
  assumed_gamma 1.0 mm_per_unit 20 max_trace_level 12
  subsurface { method Method samples Samples, Single }
}

camera { location <0.3, 1.6, -5.4> look_at <0, 1.6, 0> angle 34 right x * image_width / image_height }

// The sun low behind the leaf, a weak fill from the camera's side.
light_source { <-3, 4.2, 9> rgb <1.0, 0.92, 0.75> * 3.6 }
light_source { <6, 3, -8> rgb <0.55, 0.65, 0.9> * 0.35 }

sky_sphere {
  pigment { gradient y color_map { [0.0 rgb <0.80, 0.88, 0.97>] [0.35 rgb <0.35, 0.58, 0.90>] [1.0 rgb <0.12, 0.30, 0.70>] } }
  pigment {
    bozo turbulence 0.65 octaves 7 omega 0.55 scale <1.2, 0.3, 1.2>
    color_map { [0.0 rgbt <1, 1, 1, 1>] [0.5 rgbt <1, 1, 1, 0.85>] [0.8 rgbt <0.95, 0.95, 0.97, 0.15>] [1.0 rgbt <1, 1, 1, 0>] }
  }
}

plane { y, 0
  pigment {
    bozo scale 0.35
    color_map { [0 rgb <0.10, 0.28, 0.06>] [0.5 rgb <0.20, 0.45, 0.10>] [1 rgb <0.35, 0.55, 0.14>] }
  }
  normal { granite 0.3 scale 0.05 }
  finish { ambient 0.05 diffuse 0.8 }
}

#declare Half = 1.5;                          // half length of the leaf
#declare Width = function(x) { 0.62 * pow(max(1 - x * x / (Half * Half), 0.0001), 0.7) }
#declare Thick = function(x, z) { 0.026 * max(1 - x * x / (Half * Half), 0.0001) * max(1 - z * z / (Width(x) * Width(x)), 0.0001) + 0.003 }
#declare Arch = function(x, z) { 0.05 * x * x + 0.03 * z * z }

#declare LeafShape = isosurface {
  function { max(abs(y - Arch(x, z)) - Thick(x, z), abs(z) - Width(x)) }
  contained_by { box { <-1.55, -0.3, -0.75>, <1.55, 0.7, 0.75> } }
  max_gradient 6 accuracy 0.0002
}

// Cells: thin dark walls where the voronoi distance is small, a bright speckled chlorophyll interior elsewhere, the
// whole field distorted over several octaves.
#declare Interior = pigment {
  granite scale 0.03 turbulence 0.3
  color_map { [0 rgb <0.32, 0.62, 0.10>] [0.6 rgb <0.52, 0.80, 0.18>] [1 rgb <0.78, 0.94, 0.32>] }
}
#declare Wall = pigment { rgb <0.04, 0.20, 0.03> }
#declare Cells = pigment {
  crackle form <-1, 1, 0> scale CellScale
  #if (Warp > 0) warp { turbulence Warp octaves Octaves omega 0.6 lambda 2.1 } #end
  pigment_map { [0.00 Wall] [0.02 Wall] [0.05 Interior] [1.00 Interior] }
}

object { LeafShape
  rotate <-80, 18, -14>
  translate <0, 1.6, 0>
  texture {
    pigment { rgb <0.55, 0.78, 0.35> }             // the cuticle: a pale tint light crosses on the way in and out
    finish {
      diffuse 0.9 ambient 0 specular 0.15 roughness 0.04
      subsurface {
        translucency Mfp
        pigment { Cells }
        thickness pigment { granite scale 0.3 color_map { [0 rgb 0.4] [1 rgb 1.2] } }
        volume_sampling { depth Depth spread Spread }
      }
    }
  }
  interior { ior 1.4 }
}
