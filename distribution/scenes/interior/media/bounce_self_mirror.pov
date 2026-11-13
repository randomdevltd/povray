// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/

// A concave mirror returns an emitting cloud's warm light; self controls extra self illumination.
// Declare=Self=0 or 1 changes self illumination; Declare=Photons=0 removes the return. +w400 +h230 +pr
#version 3.7;
#ifndef (Self) #declare Self = 0; #end
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (Samples) #declare Samples = 64; #end
#ifndef (Power) #declare Power = 2400; #end
#ifndef (Cyan) #declare Cyan = 1.5; #end
global_settings {
  assumed_gamma 1 max_trace_level 10
  #if (Photons) photons { method 2 quality 1 media 40 } #end
}
background { rgb <0.002, 0.004, 0.008> }
camera { location <-5.5, 4.4, -9.8> look_at <0, 1.45, 0> right x*image_width/image_height angle 43 }
light_source { <-2, 7, -5> rgb <0.23, 0.31, 0.45> photons { reflection off refraction off } }
light_source { <-1.8, 3.8, 0.5> rgb <0.03, 0.5, 1>*Cyan
  spotlight point_at <-1.8, 1.3, 0> radius 8 falloff 15 tightness 10
  photons { reflection off refraction off }
}
#declare Metal = texture { pigment { rgb <0.09, 0.13, 0.18> } finish { diffuse 0.65 specular 0.6 roughness 0.025 } }
box { <-4.1,-0.18,-2>, <3.8,0,2> texture { Metal } }
#for (I,-3,3)
  cylinder { <I,0.006,-1.8>, <I,0.006,1.8>, 0.006 pigment { rgb <0.08,0.19,0.26> } finish { emission 0.13 } }
#end
#for (I,-1,1)
  cylinder { <-3.9,0.006,I>, <3.6,0.006,I>, 0.006 pigment { rgb <0.08,0.19,0.26> } finish { emission 0.13 } }
#end
// The warm core and the long scattering volume share this one interior.
box {
  <-3.5,0.4,-1.15>, <3.2,2.7,1.15> hollow pigment { rgbt 1 }
  interior {
    media { method 4 scattering { 1, rgb 0.18 } absorption rgb 0.025
      density { function { max(0,1-pow((y-1.5)/1.1,2)-pow(z/1.1,2))*min(1,max(0,(x+3.5)/0.7)) } }
    }
    media { method 4 emission rgb <1,0.3,0.045>*Power
      density { function { pow(max(0,1-((x-1.2)*(x-1.2)+(y-1.5)*(y-1.5)+z*z)/0.09),2) } }
    }
    light { samples Samples photons { reflection on refraction on } bounce { passes 2 self Self } }
  }
}
// The spherical cap has its paraxial focus at the warm core.
intersection {
  difference { sphere { <-0.8,1.5,0>,4.08 } sphere { <-0.8,1.5,0>,4 } }
  box { <2.75,0.2,-1.4>,<3.4,2.8,1.4> }
  pigment { rgb 0.015 } finish { diffuse 0.04 reflection 0.95 }
  photons { target reflection on collect off }
}
union {
  cylinder { <3.24,0,-1.15>,<3.24,1.5,-1.15>,0.07 }
  cylinder { <3.24,0,1.15>,<3.24,1.5,1.15>,0.07 }
  box { <2.95,0,-1.4>,<3.5,0.16,1.4> }
  texture { Metal }
}
torus { 1.37,0.035 rotate z*90 translate <2.96,1.5,0>
  pigment { rgb <0.18,0.32,0.38> } finish { diffuse 0.6 specular 0.6 }
}
cylinder { <1.2,0,0>,<1.2,1.23,0>,0.025 texture { Metal } }
cylinder { <1.2,0.01,0>,<1.2,0.1,0>,0.23 texture { Metal } }

// The hood blocks the direct warm light toward the left, leaving the mirror return visible.
difference {
  cylinder { <0.72,1.5,0>,<1.58,1.5,0>,0.39 }
  cylinder { <0.83,1.5,0>,<1.6,1.5,0>,0.345 }
  texture { Metal }
}
torus { 0.368,0.021 rotate z*90 translate <1.58,1.5,0>
  pigment { rgb <0.5,0.21,0.04> } finish { emission 0.4 diffuse 0.5 }
}
