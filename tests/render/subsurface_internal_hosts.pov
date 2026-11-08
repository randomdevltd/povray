// Hosts of a buried volume light under method 2 subsurface: every Case must render, and the exit search must find where
// each path leaves the material whatever the host's shape. S scales the whole scene with mm_per_unit, so the image is
// the same at any S. Case 4 is an open mesh, which cannot enclose a source: the parse must stop and name it.
#version 4.0;
#ifndef (Case) #declare Case=0; #end
#ifndef (S) #declare S=1; #end
#ifndef (Lm) #declare Lm=0.02; #end
global_settings { assumed_gamma 1 mm_per_unit 1000/S
 subsurface { method 2 samples 8,8 } }
background { rgb 0 }
camera { location <0,0.02,-0.06>*S look_at <0,0,0> angle 40 }
light_source { <0.05,0.08,-0.06>*S rgb 0.4 }
#declare Wax=texture { pigment { rgb <0.95,0.85,0.7> } finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { method 2 translucency <3,2,1> colour rgb 0.95 } } }
#declare Paper=texture { pigment { rgb <0.93,0.86,0.7> } finish { diffuse 0.8 subsurface { method 2 translucency <1.1,0.95,0.7> colour rgb 0.95 } } }
#macro Lamp(Lumens) pigment { rgbt 1 } hollow interior { ior off light { pigment { rgb <1,0.6,0.25> } brightness scale_lumens(Lumens*Lm, 1/S) lumens samples 4 } } #end
#macro Host(Tex) texture { Tex } interior { ior 1.45 } #end
#declare T=0.00035; // paper thickness
#macro SheetMesh(T)
 mesh2 { vertex_vectors { 8, <-0.02,-T/2,-0.015>,<0.02,-T/2,-0.015>,<0.02,-T/2,0.015>,<-0.02,-T/2,0.015>,
                           <-0.02,T/2,-0.015>,<0.02,T/2,-0.015>,<0.02,T/2,0.015>,<-0.02,T/2,0.015> }
  face_indices { 12, <0,2,1>,<0,3,2>,<4,5,6>,<4,6,7>,<0,1,5>,<0,5,4>,<1,2,6>,<1,6,5>,<2,3,7>,<2,7,6>,<3,0,4>,<3,4,7> } inside_vector y }
#end
#switch (Case)
#case (0) // control: sphere
 sphere { 0,0.01 Host(Wax) scale S } box { -0.002,0.002 Lamp(1) scale S }
#break
#case (1) // blob
 blob { threshold 0.15 sphere { 0,0.0128,1 } cylinder { <-0.01,0,0>,<0.01,0,0>,0.008,1 } Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (2) // isosurface
 isosurface { function { sqrt(x*x+y*y+z*z)-0.01 } contained_by { box { -0.011,0.011 } } max_gradient 1.1 Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (3) // closed mesh2 slab
 object { SheetMesh(0.006) Host(Wax) scale S } box { -0.001,0.001 Lamp(1) scale S }
#break
#case (4) // open mesh2 (top face removed): cannot enclose
 mesh2 { vertex_vectors { 8, <-0.02,-0.003,-0.015>,<0.02,-0.003,-0.015>,<0.02,-0.003,0.015>,<-0.02,-0.003,0.015>,
                           <-0.02,0.003,-0.015>,<0.02,0.003,-0.015>,<0.02,0.003,0.015>,<-0.02,0.003,0.015> }
  face_indices { 10, <0,2,1>,<0,3,2>,<0,1,5>,<0,5,4>,<1,2,6>,<1,6,5>,<2,3,7>,<2,7,6>,<3,0,4>,<3,4,7> } inside_vector y Host(Wax) scale S }
 box { -0.001,0.001 Lamp(1) scale S }
#break
#case (5) // superellipsoid alone
 superellipsoid { <0.3,0.3> scale <0.015,0.008,0.01> Host(Wax) scale S } box { -0.002,0.002 Lamp(1) scale S }
#break
#case (6) // superellipsoid merged with a sphere and a cylinder
 merge { superellipsoid { <0.3,0.3> scale <0.015,0.008,0.01> } sphere { <0.012,0.004,0>,0.006 } cylinder { <-0.02,0,0>,<0,0,0>,0.004 } Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (7) // CSG merge
 merge { sphere { 0,0.01 } cylinder { <-0.016,0,0>,<0.016,0,0>,0.006 } sphere { <0.016,0,0>,0.006 } Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (8) // CSG difference
 difference { box { <-0.015,-0.008,-0.01>,<0.015,0.008,0.01> } sphere { <0,0.012,0>,0.008 } cylinder { <-0.02,-0.008,0>,<0.02,-0.008,0>,0.003 } Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (9) // CSG intersection
 intersection { sphere { 0,0.012 } box { <-0.02,-0.007,-0.02>,<0.02,0.007,0.02> } cylinder { <0,-0.02,0>,<0,0.02,0>,0.0105 } Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (10) // TrueType text as the buried emitter container in 0.35 mm paper
 box { <-0.02,0,-0.015>,<0.02,T,0.015> Host(Paper) scale S }
 text { ttf "timrom.ttf" "X|<>" 1,0 scale <0.0075,0.0075,0.00012> rotate x*90 translate <-0.012,T/2+0.00006,0> Lamp(0.2) scale S }
#break
#case (11) // TrueType text as the host
 text { ttf "timrom.ttf" "IO" 1,0 scale <0.02,0.02,0.006> translate <-0.012,-0.007,-0.003> Host(Wax) scale S }
 box { <-0.0105,-0.001,-0.001>,<-0.0095,0.001,0.001> Lamp(1) scale S }
#break
#case (12) // linear sphere sweep
 sphere_sweep { linear_spline 3, <-0.015,0,0>,0.005, <0,0.002,0>,0.007, <0.015,-0.002,0>,0.004 Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (13) // cubic sphere sweep
 sphere_sweep { cubic_spline 6, <-0.03,0,0>,0.004, <-0.015,0,0>,0.005, <0,0.004,0>,0.007, <0.012,-0.003,0>,0.005, <0.02,0.002,0>,0.004, <0.03,0,0>,0.004 Host(Wax) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (14) // 0.35 mm sheet (box) with a 0.12 mm buried source
 box { <-0.02,0,-0.015>,<0.02,T,0.015> Host(Paper) scale S }
 box { <-0.008,T/2-0.00006,-0.006>,<0.008,T/2+0.00006,0.006> Lamp(0.2) scale S }
#break
#case (15) // 0.35 mm closed mesh2 sheet with a 0.12 mm buried source
 object { SheetMesh(T) Host(Paper) scale S }
 box { <-0.008,-0.00006,-0.006>,<0.008,0.00006,0.006> Lamp(0.2) scale S }
#break
#case (16) // two shells touching face to face, source in one
 box { <-0.015,-0.006,-0.01>,<0,0.006,0.01> Host(Wax) scale S }
 box { <0,-0.006,-0.01>,<0.015,0.006,0.01> Host(Wax) scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (17) // nested hosts: a core inside a shell, source in the core
 difference { sphere { 0,0.012 } sphere { 0,0.007 } Host(Wax) scale S }
 sphere { 0,0.007 Host(Paper) scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (18) // membrane lantern: volume light inside a subsurface shell lighting a closed thin mesh2 sheet
 difference { sphere { 0,0.02 } sphere { 0,0.0188 } box { <-1,0.015,-1>,<1,1,1> } Host(Paper) scale S }
 object { SheetMesh(T) scale 0.5 translate <0,-0.008,0> Host(Paper) scale S }
 sphere { 0,0.003 Lamp(2) scale S }
#break
#case (19) // candle: cylinder merged with blob drips, flame just above (not buried)
 merge {
  cylinder { <0,-0.03,0>,<0,0.0,0>,0.008 }
  blob { threshold 0.15
   cylinder { <0.0075,-0.001,0>,<0.0075,-0.012,0>,0.002,1 } sphere { <0.0075,-0.012,0>,0.0026,1 }
   cylinder { <-0.004,-0.001,0.0065>,<-0.004,-0.02,0.0065>,0.002,1 } sphere { <-0.004,-0.02,0.0065>,0.0026,1 } }
  Host(Wax) scale S }
 sphere { 0,1 scale <0.003,0.008,0.003> translate y*0.013 Lamp(2) scale S }
#break
#end
