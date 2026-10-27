// volume_light.pov: the radiance law of interior lights; volume_light.sh names the cases.
// Case: 1 a probe on an emitting slab, 2 the same scene at half scale, 3 a gas ball against an
// equal-silhouette area light, 4 the emitter's grid resolution, 5 the slab at another attenuation
// scale, 6 a ball's tangent point at the pivot radius.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (Att) #declare Att = 1; #end
#ifndef (Samples) #declare Samples = 32; #end
global_settings { assumed_gamma 1 }
#if (Case != 2)
  camera { orthographic location <0, 10, 0> sky z look_at 0 right x * 4 up z * 4 }
#end
background { rgb 0 }

// A white probe resting on an emitting slab of optical depth tau reads 2*integral of mu*(1-exp(-tau/mu)):
// its radiance straight down, saturating to L at large tau whatever the density that makes it. The slab
// is wide against its depth, so its central face behaves as infinite; the pinned resolution makes the
// half-scale room of case 2 march the same grid, and the draws are many because the probe sits inside
// the emitter's own reach, where each draw's shadow matters.
#if (Case = 1 | Case = 5)
  #ifndef (Sigma) #declare Sigma = 1; #end
  #ifndef (Depth) #declare Depth = 2; #end
  #declare Lum = 1;
  #declare Half = 64;
  box { <-Half, 0, -Half>, <Half, Depth, Half> pigment { rgbt 1 } hollow
    interior { media { method 4 resolution 0.5 absorption rgb Sigma }
      light { brightness Lum samples 512 attenuation Att } } }
  plane { y, Depth + 0.002 pigment { rgb 1 } finish { ambient 0 diffuse 1 } double_illuminate no_shadow }
#end

// The same scene at half scale: radiance anchored, so the render is bit-identical.
#if (Case = 2)
  #declare Sigma = 2;
  #declare Depth = 2;
  #declare Lum = 1;
  #declare S = 0.5;
  camera { orthographic location <0, 10 * S, 0> sky z look_at 0 right x * 4 * S up z * 4 * S }
  box { <-64 * S, 0, -64 * S>, <64 * S, Depth * S, 64 * S> pigment { rgbt 1 } hollow
    interior { media { method 4 resolution 0.25 absorption rgb Sigma }
      light { brightness Lum samples 512 } } }
  plane { y, Depth * S + 0.001 pigment { rgb 1 } finish { ambient 0 diffuse 1 } double_illuminate no_shadow }
#end

// A glowing gas ball and an area light of the same radiance over the same silhouette: one lights
// the floor exactly as the other. The light's colour is the radiance times the silhouette.
#if (Case = 3 | Case = 4)
  #declare Lum = 16;
  #declare R = 0.25;
  #declare Height = 2;
  box { <-2, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
  #if (Case = 3)
    #ifndef (Area)
      sphere { 0, R pigment { rgbt 1 } hollow no_image
        interior { light { brightness Lum samples 16 } } translate y * Height }
    #else
      light_source { y * Height, rgb Lum * R * R area_light x * R, z * R, 8, 8 circular shadowless fade_distance 0 fade_power 2 }
    #end
  #else
    sphere { 0, R pigment { rgbt 1 } hollow no_image
      interior { media { method 4 resolution Res absorption rgb 1 } light { brightness Lum samples 128 } } translate y * Height }
  #end
#end

// The pivot's other half: a ball resting on the floor leaves the tangent point at exactly its
// bounding radius, where the attenuation factor is one whatever its scale.
#if (Case = 6)
  #declare Lum = 4;
  #declare R = 1;
  sphere { 0, R pigment { rgbt 1 } hollow no_image
    interior { light { brightness Lum samples 16 attenuation Att } } translate y * R }
  plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#end

// Samples are draws per lit point, never light: the same flame at a handful of draws and at
// hundreds renders the same floor, though its glow medium rides every shadow ray. The ball carries
// an emission medium, the shape whose glow a lighting-ray march would add once per draw.
#if (Case = 7)
  #declare Lum = 3;
  #declare R = 0.16;
  box { <-2, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
  sphere { 0, 1 pigment { rgbt 1 } hollow
    interior {
      media { mix add emission rgb <1, 0.45, 0.12> * 6 density { spherical color_map { [0 rgb 0] [0.4 rgb 0.3] [1 rgb 1] } } }
      light { brightness Lum samples Samples }
    }
    scale <0.4, 1, 0.4> translate y * 1.45 }
#end

// The far field keeps the emitter's angular extent: an edge near a receiving surface casts a penumbra
// as wide as the emitter's angular diameter times the edge's offset from the surface. Case 8 is a bulb
// of a tenth radius three metres out, a plate edge 3 cm off a wall; case 9 is the sun, radius 0.6957
// at 149.6 (215 radii), over a gnomon-style edge half a metre off its dial. The dial faces the
// emitter, the edge rides its face, and the camera looks past the edge at the shadow line it casts.
#if (Case = 8 | Case = 9)
  #if (Case = 8)
    #declare R = 0.1;
    #declare D = 3;
    #declare Off = 0.03;
    #declare Frame = 0.012;
  #else
    #declare R = 0.6957;
    #declare D = 149.6;
    #declare Off = 0.5;
    #declare Frame = 0.03;
  #end
  camera { orthographic location <0, 0.895, 2> direction <0, -0.1937, -0.981> right y * Frame up x * Frame }
  background { rgb 0 }
  plane { z, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
  box { <-2, -2, Off>, <2, 0.5, Off + 0.001> pigment { rgb 1 } }
  sphere { 0, R pigment { rgbt 1 } hollow no_image
    interior { light { brightness 4 samples 16 } } translate <0, 0.5, D> }
#end
