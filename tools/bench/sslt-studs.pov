// Concave creases on one subsurface object: rows of studs at three pitches on a slab, a step and a slot; Fillet=1 rounds
// their inner corners, Sink=0 lays their bases on the slab's top, and Zoom=4 looks closely at the middle row's first studs.
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
global_settings {
  assumed_gamma 1.0
  mm_per_unit 10
  #if (Subsurface) subsurface { samples Diffuse, Single method Method spacing Spacing } #end
}

#declare Target = (Zoom = 1 ? <0, 0.3, 0.1> : <-1.7, 0.5, -0.1>);
camera { perspective location <0.8, 4.2, -4.2> look_at Target angle 42 / Zoom right x * 4 / 3 }

light_source {
  <-3, 5, -2.5>, rgb <1.0, 0.93, 0.85> * 1.2
  area_light <0.4, 0, 0>, <0, 0, 0.4>, 4, 4
  adaptive 1 circular orient
}
light_source { <3.5, 2.2, 1.0>, rgb <0.7, 0.8, 1.0> * 0.5 }
plane { y, 0 pigment { rgb 0.3 } }

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
  box { <-2.4, 0, -1.6>, <2.4, 0.4, 1.6> }
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
  texture { pigment { rgb <0.85, 0.12, 0.08> } finish { diffuse 0.75 specular 0.3 roughness 0.02 subsurface { translucency Mfp } } }
  interior { ior 1.5 }
}
