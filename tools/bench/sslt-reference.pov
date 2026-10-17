// Shapes scored against the random walks of sslt-reference.cpp (same Shape, Material, View, Zoom, Bare, Blocks, Depth;
// see doc/PERF.md): 0 a deep slab (View 1: head on), 1 a V-groove and a dimple, 2 a ridged trunk, 3 a thin wall, 4 studs.
#version 3.7;
#ifndef (Shape) #declare Shape = 0; #end
#ifndef (Material) #declare Material = 0; #end
#ifndef (View) #declare View = 0; #end
#ifndef (Zoom) #declare Zoom = 1; #end
#ifndef (Bare) #declare Bare = 0; #end
#ifndef (Blocks) #declare Blocks = 0; #end
#ifndef (Depth) #declare Depth = 24; #end
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 0; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (Facets) #declare Facets = 480; #end
#ifndef (Smooth) #declare Smooth = 0; #end
#declare MmPerUnit = array[5] { 10, 10, 80, 40, 10 };
global_settings {
  assumed_gamma 1.0
  mm_per_unit MmPerUnit[Shape]
  subsurface { samples Diffuse, Single method Method spacing Spacing }
}

// Materials: 0 red and 1 high albedo (sslt-studs.pov), 2 wax (sslt-lamps.pov), 3 bark (sslt-wide.pov), 4 clay (sslt-open.pov).
#declare Pigments = array[5] { <0.85, 0.12, 0.08>, <1.2, 1.2, 1.2>, <0.85, 0.55, 0.12>, <0.55, 0.42, 0.3>, <0.9, 0.6, 0.3> }
#declare Diffuses = array[5] { 0.75, 0.75, 0.6, 0.7, 0.7 }
#declare Mfps = array[5] { <3, 1.6, 0.9>, <2, 2, 2>, <2.5, 1.5, 0.5>, <1.2, 0.8, 0.5>, <3, 2, 1> }
#declare Iors = array[5] { 1.5, 1.5, 1.45, 1.4, 1.4 }
#declare Look = texture {
  pigment { rgb Pigments[Material] }
  finish { ambient 0 diffuse Diffuses[Material] subsurface { translucency Mfps[Material] } }
}
#declare Inside = interior { ior Iors[Material] }

#macro Warm() light_source { <-3, 5, -2.5>, rgb <1.0, 0.93, 0.85> * 1.2 } #end
#macro Cool() light_source { <3.5, 2.2, 1.0>, rgb <0.7, 0.8, 1.0> * 0.5 } #end
#macro Camera(Location, Target, Angle) camera { perspective location Location look_at Target angle Angle right x * 4 / 3 } #end

// The trunk of sslt-wide.pov, NA facets around by NH along: a closed mesh2 with ridged bark, with Smooth the surface's own normals.
#macro Trunk(Radius, Height, NA, NH)
  #local C = NA * (NH + 1);
  mesh2 {
    vertex_vectors { C + 2,
      #for (J, 0, NH) #for (K, 0, NA - 1)
        #local T = 2 * pi * K / NA; #local Y = Height * J / NH;
        #local Rr = Radius * (1 + 0.06 * sin(9 * T + 0.7 * Y) + 0.03 * sin(31 * T - 2.3 * Y) - 0.1 * J / NH);
        <Rr * cos(T), Y, Rr * sin(T)>,
      #end #end
      <0, 0, 0>, <0, Height, 0>
    }
    #if (Smooth)
      normal_vectors { C + 2,
        #for (J, 0, NH) #for (K, 0, NA - 1)
          #local T = 2 * pi * K / NA; #local Y = Height * J / NH;
          #local Rr = Radius * (1 + 0.06 * sin(9 * T + 0.7 * Y) + 0.03 * sin(31 * T - 2.3 * Y) - 0.1 * J / NH);
          #local Rt = Radius * (0.54 * cos(9 * T + 0.7 * Y) + 0.93 * cos(31 * T - 2.3 * Y));
          #local Ry = Radius * (0.042 * cos(9 * T + 0.7 * Y) - 0.069 * cos(31 * T - 2.3 * Y) - 0.1 / Height);
          <Rr * cos(T) + Rt * sin(T), -Rr * Ry, Rr * sin(T) - Rt * cos(T)>,
        #end #end
        -y, y
      }
    #end
    face_indices { 2 * NA * NH + 2 * NA,
      #for (J, 0, NH - 1) #for (K, 0, NA - 1)
        #local A0 = J * NA + K; #local A1 = J * NA + mod(K + 1, NA);
        <A0, A1, A1 + NA>, <A0, A1 + NA, A0 + NA>,
      #end #end
      #for (K, 0, NA - 1) <C, K, mod(K + 1, NA)>, <C + 1, NH * NA + K, NH * NA + mod(K + 1, NA)>, #end
    }
    #if (Smooth)
      normal_indices { 2 * NA * NH + 2 * NA,
        #for (J, 0, NH - 1) #for (K, 0, NA - 1)
          #local A0 = J * NA + K; #local A1 = J * NA + mod(K + 1, NA);
          <A0, A1, A1 + NA>, <A0, A1 + NA, A0 + NA>,
        #end #end
        #for (K, 0, NA - 1) <C, C, C>, <C + 1, C + 1, C + 1>, #end
      }
    #end
    inside_vector y
  }
#end

#switch (Shape)
#case (0)
  box { <-12, -Depth, -12>, <12, 0, 12> texture { Look } interior { Inside } }
  #if (View = 1)
    Camera(<0, 4, -0.01>, <0, 0, 0>, 30)
    light_source { <0, 1000, 0>, rgb 1 }
  #else
    Camera(<0, 2.5, -3>, <0, 0, 0>, 40)
    Warm() Cool()
  #end
#break
#case (1)
  #declare S = 0.2 / sqrt(2);
  difference {
    box { <-2.4, -2, -1.6>, <2.4, 0, 1.6> }
    box { <-3, -S, -S>, <3, S, S> rotate x * 45 translate <0, 0, -0.6> }
    sphere { <0.9, 0.12, 0.5>, 0.3 }
    texture { Look } interior { Inside }
  }
  Camera(<0.8, 3.5, -3.5>, <0.3, -0.1, -0.05>, 30 / Zoom)
  Warm() Cool()
#break
#case (2)
  object { Trunk(0.35, 2, Facets, Facets / 5) texture { Look } interior { Inside } }
  Camera(<0, 1, -2>, <0, 1, 0>, 36 / Zoom)
  light_source { <-3, 4, -4>, rgb <1.0, 0.9, 0.8> * 1.2 }
  light_source { <4, 1.5, 2>, rgb <0.6, 0.7, 1.0> * 0.5 }
#break
#case (3)
  box { <-1, 0, -0.015>, <1, 1.2, 0.015> rotate y * 30 texture { Look } interior { Inside } }
  Camera(<0.6, 1, -3>, <0, 0.6, 0>, 40 / Zoom)
  light_source { <-2, 3, -3>, rgb <1.0, 0.93, 0.85> }
  light_source { <1.5, 2, 3>, rgb <0.7, 0.8, 1.0> * 0.8 }
#break
#else
  #declare R = 0.24;
  #declare H = 0.17;
  #declare Sink = 0.002;
  merge {
    #if (Blocks)
      box { <-2.4, 0, -1.6>, <0, 0.6, 1.6> }
      box { <0.05, 0, -1.6>, <2.4, 0.35, 1.6> }
    #else
      box { <-2.4, 0, -1.6>, <2.4, 0.4, 1.6> }
    #end
    #if (!Bare & !Blocks)
      #for (I, 0, 4) cylinder { <-2.0 + I * 0.8, 0.4 - Sink, -1.1>, <-2.0 + I * 0.8, 0.4 + H, -1.1>, R } #end
      #for (I, 0, 6) cylinder { <-2.0 + I * 0.56, 0.4 - Sink, -0.15>, <-2.0 + I * 0.56, 0.4 + H, -0.15>, R } #end
      #for (I, 0, 6) cylinder { <-2.0 + I * 0.51, 0.4 - Sink, 0.75>, <-2.0 + I * 0.51, 0.4 + H, 0.75>, R } #end
      box { <-2.4, 0.4 - Sink, 1.2>, <2.4, 0.75, 1.6> }
      difference {
        box { <1.9, 0.4 - Sink, -1.6>, <2.4, 0.75, 1.2 + Sink> }
        box { <2.1, 0.5, -1.7>, <2.2, 0.8, 1.21> }
      }
    #end
    texture { Look } interior { Inside }
  }
  #declare Target = (View = 1 ? <2.15, 0.55, -0.6> : View = 2 ? <-1.49, 0.5, 0.75> : View = 3 ? <0.02, 0.4, -0.4> : Zoom = 1 ? <0, 0.3, 0.1> : <-1.7, 0.5, -0.1>);
  Camera(<0.8, 4.2, -4.2>, Target, 42 / Zoom)
  Warm() Cool()
#end
