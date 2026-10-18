// Subsurface hits of low ray weight: Case 0 six stacked leaves passing part of the light, lit from behind; Case 1 wax spheres
// between facing mirrors over a glossy floor; Case 2 eight face-on discs, Direct=1 showing each. See doc/PERF.md.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 12; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (Leaves) #declare Leaves = 6; #end
#ifndef (Through) #declare Through = 0.4; #end
#ifndef (Mirror) #declare Mirror = 0.7; #end
#ifndef (Direct) #declare Direct = 0; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 20
  max_trace_level (Case = 2 ? 40 : 24)
  #if (Subsurface)
    subsurface { samples Diffuse, Single method Method spacing Spacing }
  #end
}

#if (Case = 0)
camera { location <0, 0, -4> look_at <0, 0, 0> angle 40 right x * image_width / image_height }
light_source { <-2, 3, 12> rgb <1, 0.95, 0.8> * 1.6 area_light <1, 0, 0>, <0, 1, 0>, 2, 2 adaptive 0 }
light_source { <3, 2, -8> rgb <0.6, 0.7, 0.9> * 0.3 }
background { rgb <0.55, 0.7, 0.9> }
#declare Mid = 0.04;
#declare LensR = (1 + (Mid / 2) * (Mid / 2)) / Mid;
#declare Leaf = intersection {
  sphere { <0, LensR - Mid / 2, 0>, LensR }
  sphere { <0, -(LensR - Mid / 2), 0>, LensR }
  scale <1.2, 1, 0.5>
  rotate -x * 90
}
#declare Seed = seed(5);
#for (I, 0, Leaves - 1)
  object { Leaf
    rotate z * (35 * I + 20 * rand(Seed))
    translate <0.35 * (rand(Seed) - 0.5), 0.35 * (rand(Seed) - 0.5), 0.12 * I>
    texture {
      pigment { rgbt <0.45, 0.75, 0.25, Through> }
      finish { ambient 0 diffuse 0.9 subsurface { translucency 1.0 #ifdef (MinimumInfluence) minimum_influence MinimumInfluence #end } }
    }
    interior { ior 1.4 }
  }
#end
#elseif (Case = 2)
camera { location <0, 0, -4> look_at <0, 0, 0> angle 40 right x * image_width / image_height }
light_source { <-2, 3, 12> rgb <1, 0.95, 0.8> * 1.6 area_light <1, 0, 0>, <0, 1, 0>, 2, 2 adaptive 0 }
light_source { <3, 2, -8> rgb <0.6, 0.7, 0.9> * 0.3 }
background { rgb <0.55, 0.7, 0.9> }
#declare Step = (Direct ? 0.13 : 0);
// A disc's two faces each pass the root of Through, so the disc passes Through.
#for (I, 0, 7)
  cylinder { <0, 0, 0>, <0, 0, 0.03>, (Direct ? 0.8 : 0.9)
    translate <Step * (I - 3.5), 0.5 * Step * (I - 3.5), 0.1 * I>
    texture {
      pigment { rgbt <0.45, 0.75, 0.25, sqrt(Through)> }
      finish { ambient 0 diffuse 0.9 subsurface { translucency 1.0 #ifdef (MinimumInfluence) minimum_influence MinimumInfluence #end } }
    }
    interior { ior 1.4 }
  }
#end
#else
camera { location <0.6, 0.9, -1.0> look_at <-0.3, 0.4, 0.6> angle 60 right x * image_width / image_height }
light_source { <1.5, 4, -2> rgb 1.2 area_light <0.6, 0, 0>, <0, 0, 0.6>, 2, 2 adaptive 0 }
light_source { <-1, 1.5, -1.5> rgb <1, 0.85, 0.7> * 0.3 }
background { rgb 0.05 }
plane { y, 0 pigment { rgb <0.3, 0.25, 0.2> } finish { diffuse 0.7 reflection 0.06 } }
#declare Glass = finish { ambient 0 diffuse 0.02 reflection Mirror }
box { <-2, 0, 1.2>, <2, 2, 1.25> pigment { rgb 1 } finish { Glass } }
box { <-2, 0, -1.25>, <2, 2, -1.2> pigment { rgb 1 } finish { Glass } }
#declare Seed = seed(3);
#for (I, 0, 4)
  sphere { <-1.2 + 0.6 * I, 0.22, 0.4 * (rand(Seed) - 0.5)>, 0.2
    pigment { rgb <0.95, 0.75 - 0.1 * I, 0.45 + 0.08 * I> }
    finish { ambient 0 diffuse 0.8 specular 0.2 roughness 0.02 subsurface { translucency <2, 1.2, 0.7> #ifdef (MinimumInfluence) minimum_influence MinimumInfluence #end } }
    interior { ior 1.45 }
  }
#end
#end
