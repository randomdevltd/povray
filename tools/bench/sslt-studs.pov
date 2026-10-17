// Concave creases on one subsurface object: studs at three pitches, a step and a slot on a slab; Fillet, Sink, Zoom, View (1 slot,
// 2 tight row, 3 gap), Bare (slab alone), Blocks (two blocks with an air gap) and Reference (as tools/bench/sslt-reference.cpp).
#version 3.7;
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 21; #end
#ifndef (Method) #declare Method = 1; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
#ifndef (Fillet) #declare Fillet = 0; #end
#ifndef (Zoom) #declare Zoom = 1; #end
#ifndef (Mfp) #declare Mfp = <3, 1.6, 0.9>; #end
#ifndef (Sink) #declare Sink = 0.002; #end
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Bare) #declare Bare = 0; #end
#ifndef (Blocks) #declare Blocks = 0; #end
#ifndef (Colour) #declare Colour = <0.85, 0.12, 0.08>; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 10
  #if (Subsurface) subsurface { samples Diffuse, Single method Method spacing Spacing } #end
}

#ifndef (View) #declare View = 0; #end
#declare Target = (View = 1 ? <2.15, 0.55, -0.6> : View = 2 ? <-1.49, 0.5, 0.75> : View = 3 ? <0.02, 0.4, -0.4> : Zoom = 1 ? <0, 0.3, 0.1> : <-1.7, 0.5, -0.1>);
camera { perspective location <0.8, 4.2, -4.2> look_at Target angle 42 / Zoom right x * 4 / 3 }

light_source {
  <-3, 5, -2.5>, rgb <1.0, 0.93, 0.85> * 1.2
  #if (!Reference) area_light <0.4, 0, 0>, <0, 0, 0.4>, 4, 4 adaptive 1 circular orient #end
}
light_source { <3.5, 2.2, 1.0>, rgb <0.7, 0.8, 1.0> * 0.5 }
#if (!Reference) plane { y, 0 pigment { rgb 0.3 } } #end

#declare R = 0.24;
#declare H = 0.17;
#declare F = 0.06;
#macro Stud(P)
  cylinder { P - y * Sink, P + <0, H, 0>, R }
  #if (Fillet)
    difference {
      cylinder { P - y * Sink, P + <0, F, 0>, R + F }
      torus { R + F, F translate P + <0, F, 0> }
    }
  #end
#end
#macro Crease(Lo, Hi, Axis, Centre)
  #if (Fillet) difference { box { Lo - y * Sink, Hi } cylinder { Centre - Axis * 3, Centre + Axis * 3, F } } #end
#end

merge {
  #if (Blocks)
  box { <-2.4, 0, -1.6>, <0, 0.6, 1.6> }
  box { <0.05, 0, -1.6>, <2.4, 0.35, 1.6> }
  #else
  box { <-2.4, 0, -1.6>, <2.4, 0.4, 1.6> }
  #end
  #if (!Bare & !Blocks)
  #for (I, 0, 4) Stud(<-2.0 + I * 0.8, 0.4, -1.1>) #end
  #for (I, 0, 6) Stud(<-2.0 + I * 0.56, 0.4, -0.15>) #end
  #for (I, 0, 6) Stud(<-2.0 + I * 0.51, 0.4, 0.75>) #end
  box { <-2.4, 0.4 - Sink, 1.2>, <2.4, 0.75, 1.6> }
  difference {
    box { <1.9, 0.4 - Sink, -1.6>, <2.4, 0.75, 1.2 + Sink> }
    box { <2.1, 0.5, -1.7>, <2.2, 0.8, 1.21> }
  }
  Crease(<-2.4, 0.4, 1.2 - F>, <1.9, 0.4 + F, 1.2>, x, <0, 0.4 + F, 1.2 - F>)
  Crease(<1.9 - F, 0.4, -1.6>, <1.9, 0.4 + F, 1.2>, z, <1.9 - F, 0.4 + F, 0>)
  #end
  texture { pigment { rgb Colour } finish { #if (Reference) ambient 0 #end diffuse 0.75 specular 0.3 * !Reference roughness 0.02 subsurface { translucency Mfp } } }
  interior { ior 1.5 }
}
