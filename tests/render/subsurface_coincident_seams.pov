// Coincident and coplanar surfaces inside CSG hosts of a buried volume light under method 2 subsurface: halves cut on
// one plane, shells sharing a face, equal curved surfaces, T-junctions, grazing and nested seams. Every Case must render,
// and its image must be the same at any S (the whole scene scales with mm_per_unit).
#version 4.0;
#ifndef (Case) #declare Case=0; #end
#ifndef (S) #declare S=1; #end
#ifndef (Lm) #declare Lm=0.02; #end
global_settings { assumed_gamma 1 mm_per_unit 1000/S
 subsurface { method 2 samples 8,8 } }
background { rgb 0 }
camera { location <0.012,0.03,-0.05>*S look_at <0,0,0> angle 45 }
light_source { <0.05,0.08,-0.06>*S rgb 0.4 }
#declare Wax=texture { pigment { rgb <0.95,0.85,0.7> } finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { method 2 translucency <3,2,1> colour rgb 0.95 } } }
#macro Lamp(Lumens) pigment { rgbt 1 } hollow interior { ior off light { pigment { rgb <1,0.6,0.25> } brightness scale_lumens(Lumens*Lm, 1/S) lumens samples 4 } } #end
#macro Host() texture { Wax } interior { ior 1.45 } #end
#declare Half=box { <-0.012,-0.006,-0.009>,<0,0.006,0.009> }
#declare Other=box { <0,-0.006,-0.009>,<0.012,0.006,0.009> }
// a half-cylinder shell (0.9 mm wall) cut on the plane x = 0
#macro HalfShell(C,R,Side)
 intersection { difference { cylinder { C-z*0.009,C+z*0.009,R+0.00045 } cylinder { C-z,C+z,R-0.00045 } } plane { Side*x,0 } }
#end
#switch (Case)
#case (0) // merge of two halves cut on one plane
 merge { object { Half } object { Other } Host() scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (1) // union of two halves cut on one plane
 union { object { Half } object { Other } Host() scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (2) // difference whose cutter shares a face with the host and halves sharing the cut plane
 difference { merge { object { Half } object { Other } } box { <0,0.002,-0.01>,<0.013,0.007,0.01> } Host() scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (3) // half-cylinder shells sharing their cut faces, source in the wall
 merge { HalfShell(<0,0,0>,0.008,1) HalfShell(<0,0,0>,0.008,-1) Host() scale S }
 box { <-0.0082,-0.0003,-0.003>,<-0.0078,0.0003,0.003> Lamp(0.2) scale S }
#break
#case (4) // two spheres of equal radius on the same centre
 merge { sphere { 0,0.01 } sphere { 0,0.01 } Host() scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (5) // cylinder standing with its cap on a box face
 merge { box { <-0.012,-0.006,-0.009>,<0.012,0,0.009> } cylinder { <0,0,0>,<0,0.008,0>,0.005 } Host() scale S }
 box { <-0.002,-0.002,-0.002>,<0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (6) // T-junction: a third box meets the shared face of two halves edge-on
 merge { object { Half } object { Other } box { <-0.004,0.006,-0.009>,<0.004,0.012,0.009> } Host() scale S }
 box { <-0.002,0.004,-0.002>,<0.002,0.008,0.002> Lamp(1) scale S }
#break
#case (7) // a seam the camera and light see at grazing incidence: halves cut on a plane through both
 merge { object { Half rotate y*-14 } object { Other rotate y*-14 } Host() rotate x*-31 scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (8) // nested seams: a merge of halves inside a merge of halves, each cut on the plane x = 0
 merge { merge { object { Half } object { Other } } merge { object { Half scale 0.5 } object { Other scale 0.5 } } Host() scale S }
 box { <-0.004,-0.002,-0.002>,<-0.002,0.002,0.002> Lamp(1) scale S }
#break
#case (9) // a rolled sheet of offset half-shells overlapping 0.1 mm across their cuts, source in the outer wall
 merge {
  #for (K,0,3)
   #local R=0.0085-K*0.0007;
   #local C=<0,(mod(K,2)=0?0:0.0007),0>;
   intersection { difference { cylinder { C-z*0.009,C+z*0.009,R+0.00045 } cylinder { C-z,C+z,R-0.00045 } } plane { (mod(K,2)=0?x:-x),0.00005 } }
  #end
  Host() scale S }
 box { <-0.0087,-0.0002,-0.003>,<-0.0083,0.0002,0.003> Lamp(0.2) scale S }
#break
// outer faces shared by equal shapes whose inside tests carry a band: the merged face must look as one shape's
#case (10) // two equal cylinders
 merge { cylinder { -y*0.006,y*0.006,0.008 } cylinder { -y*0.006,y*0.006,0.008 } Host() scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (11) // the reference: one cylinder
 merge { cylinder { -y*0.006,y*0.006,0.008 } Host() scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (12) // two equal blobs, as a union: a merge drops both faces of equal blobs for every ray, camera rays included
 #declare Drop=blob { threshold 0.5 sphere { 0,0.012,1 } sphere { x*0.004,0.01,1 } }
 union { object { Drop } object { Drop } Host() scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#case (13) // the reference: one blob
 #declare Drop=blob { threshold 0.5 sphere { 0,0.012,1 } sphere { x*0.004,0.01,1 } }
 union { object { Drop } Host() scale S }
 box { -0.002,0.002 Lamp(1) scale S }
#break
#end
