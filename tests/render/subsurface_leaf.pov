// A thin leaf, its flesh a distorted voronoi field read in a thin slice by volume_sampling, lit by two prism spectra (photons):
// Front on its camera side, and Back behind it, which reaches the camera only by diffusing through the leaf.
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
#ifndef (Front) #declare Front = 1; #end
#ifndef (Back) #declare Back = 1; #end
#ifndef (PhotonCount) #declare PhotonCount = 400000; #end
#ifndef (Dispersion) #declare Dispersion = 1.04; #end
#ifndef (Bands) #declare Bands = 61; #end
#ifndef (FrontPower) #declare FrontPower = 18; #end
#ifndef (BackPower) #declare BackPower = 40; #end
#ifndef (SunPower) #declare SunPower = 1.4; #end
#ifndef (Sun) #declare Sun = 1; #end                // 0 for the prism's light alone, with Fill=0
#ifndef (Fill) #declare Fill = 1; #end
#ifndef (Gloss) #declare Gloss = 0; #end              // photons also add a specular highlight, which would mimic diffusion
#ifndef (Collect) #declare Collect = 1; #end        // 0: the leaf ignores photons, as a control
#ifndef (FrontX) #declare FrontX = -2.45; #end        // each spectrum lands about 1.65 units on from its prism
#ifndef (BackX) #declare BackX = 2.45; #end
#ifndef (PrismDist) #declare PrismDist = 2.2; #end
global_settings {
  assumed_gamma 1.0 mm_per_unit 20 max_trace_level 12
  subsurface { method Method samples Samples, Single }
  #if (Front | Back) photons { count PhotonCount gather 0, 20000 radius 0.025 autostop 0 } #end
}

camera { location <0.3, 1.6, -5.4> look_at <0, 1.6, 0> angle 34 right x * image_width / image_height }

// The sun low behind the leaf, a weak fill from the camera's side.
#if (Sun) light_source { <-3, 4.2, 9> rgb <1.0, 0.92, 0.75> * SunPower photons { refraction off reflection off } } #end
#if (Fill) light_source { <6, 3, -8> rgb <0.55, 0.65, 0.9> * 0.35 photons { refraction off reflection off } } #end

#ifndef (Sky) #declare Sky = 1; #end                // 0 with Sun=0 Fill=0: nothing but the prisms lights the leaf
#if (Sky)
sky_sphere {
  pigment { gradient y color_map { [0.0 rgb <0.80, 0.88, 0.97>] [0.35 rgb <0.35, 0.58, 0.90>] [1.0 rgb <0.12, 0.30, 0.70>] } }
  pigment {
    bozo turbulence 0.65 octaves 7 omega 0.55 scale <1.2, 0.3, 1.2>
    color_map { [0.0 rgbt <1, 1, 1, 1>] [0.5 rgbt <1, 1, 1, 0.85>] [0.8 rgbt <0.95, 0.95, 0.97, 0.15>] [1.0 rgbt <1, 1, 1, 0>] }
  }
}
#end

plane { y, 0                                  // dry mud: dark cracks round paler, mottled plates
  pigment {
    crackle scale 0.45 turbulence 0.25 octaves 3
    color_map { [0.00 rgb <0.07, 0.05, 0.035>] [0.05 rgb <0.12, 0.085, 0.055>] [0.12 rgb <0.36, 0.26, 0.17>] [0.6 rgb <0.45, 0.33, 0.22>] [1.00 rgb <0.52, 0.40, 0.28>] }
  }
  normal { crackle 0.6 scale 0.45 turbulence 0.25 octaves 3 }
  finish { ambient 0.05 diffuse 0.8 }
  photons { collect off }
}

// The leaf is a lens: two large spheres intersected, so its two faces meet in a knife edge all round. Scaled to 3 units long
// and 1.24 wide, with a middle 0.06 thick (1.2 mm), the wall thins toward the rim as 1 - r^2.
#ifndef (Mid) #declare Mid = 0.06; #end
#declare LensR = (1 + (Mid / 2) * (Mid / 2)) / Mid;
#declare LeafShape = intersection {
  sphere { <0, LensR - Mid / 2, 0>, LensR }
  sphere { <0, -(LensR - Mid / 2), 0>, LensR }
  scale <1.5, 1, 0.62>
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
      diffuse 0.9 ambient 0 specular Gloss roughness 0.04
      subsurface {
        translucency Mfp
        pigment { Cells }
        thickness pigment { granite scale 0.3 color_map { [0 rgb 0.4] [1 rgb 1.2] } }
        volume_sampling { depth Depth spread Spread }
      }
    }
  }
  interior { ior 1.4 }
  photons { collect Collect }
}

// A beam through a 60 degree prism turned for minimum deviation, a band as tall as the leaf is wide. Dir 1 runs along +z from
// the camera's side; -1 runs along -z from behind. The spectrum swings toward the leaf and lands on the face turned to it.
#macro PrismBeam(X, Dir, Pw)
  #local Z = -Dir * PrismDist;
  prism { linear_sweep linear_spline -0.9, 0.9, 4
    <-0.693, 0>, <0.346, 0.6>, <0.346, -0.6>, <-0.693, 0>
    pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 specular 0.3 roughness 0.005 reflection { 0, 1 fresnel on } conserve_energy }
    interior { ior 1.5 dispersion Dispersion dispersion_samples Bands }
    rotate y * (Dir > 0 ? 0 : 180)
    translate <X, 1.6, Z>
    photons { target refraction on reflection off collect off }
  }
  light_source { <X, 1.6, -20 * Dir> rgb Pw parallel point_at <X, 1.6, Z>
    projected_through { box { <X - 0.15, 0.7, min(Z - 0.9 * Dir, Z - 0.88 * Dir)>, <X + 0.15, 2.5, max(Z - 0.9 * Dir, Z - 0.88 * Dir)> } }
    photons { refraction on reflection off }
  }
  // An unseen wall in front of the light stops its ordinary light reaching anything; its photons pass through it.
  box { <X - 12, -1, min(-Dir * 19.5, -Dir * 19.4)>, <X + 12, 8, max(-Dir * 19.5, -Dir * 19.4)> pigment { rgb 0 } no_image no_reflection photons { pass_through } }
#end
#if (Front) PrismBeam(FrontX, 1, FrontPower) #end
#if (Back) PrismBeam(BackX, -1, BackPower) #end
