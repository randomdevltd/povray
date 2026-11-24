// Buried veins: a hand lit from inside by one volume light whose pigment draws a vein network under the skin.
// Units are metres; subsurface distances are millimetres (mm_per_unit 1000). See buried-veins.md.
#version 4.0;
#include "functions.inc"
#ifndef (View) #declare View=0; #end              // 0 the whole hand, 1 close-up of the knuckles and the back of the hand
#ifndef (Cells) #declare Cells=400000; #end       // the hand's map points: about sqrt(49000/Cells) mm apart
#ifndef (Samples) #declare Samples=16; #end       // the vein light's samples: paths each map point walks
#ifndef (Seed) #declare Seed=7; #end              // places the vein pattern
#ifndef (Fill) #declare Fill=1; #end              // the room light; 0 leaves only the veins
#ifndef (VeinNits) #declare VeinNits=1; #end      // the vein light's brightness
#ifndef (BranchGain) #declare BranchGain=2; #end   // emission of branches and capillaries against trunks,
#ifndef (CapillaryGain) #declare CapillaryGain=5; #end  // since thinner lines give off less light in all
#ifndef (Normals) #declare Normals=1; #end
#declare Flesh=<0.85,0.6,0.5>;                    // the flesh under the skin
#ifndef (LightCells) #declare LightCells=16000000; #end  // the vein light's source cells: 0.48 mm across

global_settings { assumed_gamma 1 mm_per_unit 1000 max_trace_level 12 subsurface { method 2 samples 16, 16 } }
background { rgb <0.012,0.014,0.02> }

#switch (View)
#case (0) camera { location <0.05,0.25,-0.20> look_at <0.0,0.012,-0.012> angle 44 right x*image_width/image_height } #break
#case (1) camera { location <0.004,0.17,-0.055> look_at <0.004,0.022,-0.002> angle 30 right x*image_width/image_height } #break
#end

#if (Fill > 0)
// a soft warm key from the front left, and a cool rim from behind that outlines the fingers
light_source { <-0.45,0.55,-0.5> rgb <1,0.86,0.74>*0.22*Fill area_light x*0.25,z*0.25,5,5 adaptive 1 jitter circular orient }
light_source { <0.35,0.18,0.55> rgb <0.55,0.65,1>*0.25*Fill area_light x*0.1,y*0.1,3,3 adaptive 1 jitter }
#end

plane { y,0
 pigment { granite turbulence 0.4 scale 0.05 colour_map { [0 rgb <0.035,0.033,0.036>] [1 rgb <0.06,0.055,0.058>] } }
 normal { granite 0.1 scale 0.004 }
 finish { ambient 0 diffuse 0.7 specular 0.06 roughness 0.05 }
}

// ================================================================ hand (right hand, palm down, fingers toward -x, back facing +y)
#declare FZ=array[4] {-0.027,-0.009,0.009,0.026};
#declare FL=array[4][3] {{0.040,0.025,0.020},{0.045,0.028,0.021},{0.042,0.027,0.020},{0.033,0.021,0.018}};
#declare FR=array[4][3] {{0.0090,0.0080,0.0070},{0.0092,0.0082,0.0072},{0.0086,0.0076,0.0067},{0.0076,0.0067,0.0060}};
#declare FS=array[4] {-14,-4,5,15};
#declare FH=array[4] {0.020,0.016,0.0095,0.0072}; // joint heights: knuckle, PIP, DIP, tip
#declare FJ=array[4][4];
#for (J,0,3)
 #declare FJ[J][0]=<-0.040,FH[0],FZ[J]>;
 #local D=vrotate(-x,y*FS[J]);
 #for (K,1,3) #local P=FJ[J][K-1]+D*FL[J][K-1]*(K=3?0.85:1); #declare FJ[J][K]=<P.x,FH[K]+(J=3?-0.0012:0),P.z>; #end
#end
#declare Thumb=array[4] {<0.040,0.014,-0.022>,<0.005,0.013,-0.045>,<-0.025,0.012,-0.060>,<-0.046,0.010,-0.066>};
// A linear sphere sweep written as its spheres and the cones tangent to each pair: the same solid, quicker to intersect,
// which matters here because every internal path tests the hand's surface at each step.
#macro Seg(P1,R1,P2,R2)
 #local D=vlength(P2-P1); #local U=(P2-P1)/D; #local S=(R1-R2)/D;
 cone { P1+U*R1*S,R1*sqrt(1-S*S), P2+U*R2*S,R2*sqrt(1-S*S) }
 sphere { P2,R2 }
#end
#macro Sweep(P,Rs,N)
 sphere { P[0],Rs[0] }
 #for (K,1,N-1) Seg(P[K-1],Rs[K-1],P[K],Rs[K]) #end
#end
#macro HandShape(I)
 merge {
  sphere { 0,1 scale <0.050-I,0.0125-I,0.040-I> translate <0.004,0.0135,0> }
  sphere { <-0.034,0.017,-0.027>,0.0095-I } Seg(<-0.034,0.017,-0.027>,0.0095-I,<-0.034,0.016,0.026>,0.0085-I)
  sphere { 0,1 scale <0.030-I,0.0115-I,0.017-I> translate <0.018,0.0125,-0.026> }
  sphere { <0.050,0.022,0.0>,0.0205-I } Seg(<0.050,0.022,0.0>,0.0205-I,<0.15,0.025,0.004>,0.020-I)
  #for (J,0,3)
   #local P=array[5] {<0.030,0.019,FZ[J]*0.6>,FJ[J][0],FJ[J][1],FJ[J][2],FJ[J][3]};
   #local Rs=array[5] {0.0075-I,0.0095-I,FR[J][1]*1.04-I,FR[J][2]*1.0-I,FR[J][2]*0.88-I};
   Sweep(P,Rs,5)
  #end
  #local Rs=array[4] {0.0125-I,0.0108-I,0.0094-I,0.0082-I};
  Sweep(Thumb,Rs,4)
 }
#end
#declare HandObj=object { HandShape(0) };

// skin: the texture pigment is a thin warm skin over pale flesh; red light spreads farthest, blue least
#declare SkinTex=texture {
 pigment { granite turbulence 0.3 scale 0.02 colour_map { [0 rgb <0.80,0.52,0.42>] [0.5 rgb <0.76,0.47,0.38>] [1 rgb <0.83,0.55,0.45>] } }
 #if (Normals) normal { average normal_map {
   [1 bumps 0.3 scale 0.00028]
   [1 wrinkles 0.4 scale <0.0012,0.004,0.010>]
   [0.6 granite 0.2 scale 0.0015] } } #end
 finish { ambient 0 diffuse 0.7 specular 0.2 roughness 0.035 subsurface { translucency <2.2,1.0,0.55> colour rgb Flesh thickness 0.4 } }
};

// ================================================================ veins
// One buried volume light fills a layer 0.3 to 1.2 mm under the skin of the back of the hand, and one pigment decides
// where in it light is given off: thin bright lines on black. Trunks follow set paths; branches and capillaries are the
// zero lines of two noises, branches only near trunks and capillaries only near branches, each finer and more crowded
// than the last. The pattern is two-dimensional across the back of the hand, carried straight through the layer's depth.
#declare R=seed(Seed);
// seeded offsets into each noise (functions take plain numbers)
#declare T1=100*rand(R); #declare T2=100*rand(R); #declare T3=100*rand(R);
#declare B1=100*rand(R); #declare B2=100*rand(R); #declare B3=100*rand(R);
#declare C1=100*rand(R); #declare C2=100*rand(R); #declare C3=100*rand(R);
// Trunks are lines across the back of the hand, from side by side at the wrist (x 0.13) out to the gaps between the
// knuckles (x -0.04) and on along the fingers, each wandering by its own slow noise; one more arches across the middle.
#declare Spread=function(vX) { 3*pow(min(1,max(0,(0.11-vX)/0.15)),2)-2*pow(min(1,max(0,(0.11-vX)/0.15)),3) };
#declare Wander=function(vX,vK) { 0.010*(f_noise3d(vX*30,vK*7.3+T1,T2)-0.5) };
#declare TrunkK=function(vX,vZ,vW,vG,vK) { abs(vZ-(vW+(vG-vW)*Spread(vX)+Wander(vX,vK))) };
#declare TrunkAway=function(vX,vZ) {
 min(TrunkK(vX,vZ,-0.010,-0.040,0), TrunkK(vX,vZ,-0.004,-0.018,1), TrunkK(vX,vZ,0.003,0.000,2), TrunkK(vX,vZ,0.010,0.0175,3),
     TrunkK(vX,vZ,0.016,0.034,4),
     select(abs(vZ+0.004)-0.032, abs(vX-(0.012+0.010*(vZ+0.004)*(vZ+0.004)/0.001+Wander(vZ,5))), 1))
};
// signed noises whose zero lines are the finer vessels; a slower noise bends each one's coordinates so the lines wander
#declare Branch=function(vX,vZ) { f_noise3d(vX*70+B1+1.0*f_noise3d(vX*30,B2,vZ*30),B2,vZ*95+B3)-0.5 };
#declare Capillary=function(vX,vZ) { f_noise3d(vX*230+C1+1.0*f_noise3d(vX*90,C2,vZ*90),C2,vZ*260+C3)-0.5 };
// the distance in metres to a noise's zero line: its value over the length of its slope
#declare E=0.00005;
#declare Away=function(vN,vNx,vNz) { abs(vN)/max(1e-9,sqrt((vNx-vN)*(vNx-vN)+(vNz-vN)*(vNz-vN))/E) };
#declare BranchAway=function(vX,vZ) { Away(Branch(vX,vZ),Branch(vX+E,vZ),Branch(vX,vZ+E)) };
#declare CapillaryAway=function(vX,vZ) { Away(Capillary(vX,vZ),Capillary(vX+E,vZ),Capillary(vX,vZ+E)) };
// a vessel's profile across it: 1 on its centre line, 0 at its half-width W
#declare Line=function(vD,vW) { pow(max(0,1-vD/vW),1.5) };
// Trunks are 2.4 to 1.6 mm across. Branches, 1.4 mm across where they leave a trunk and thinning away from it, grow only
// within 7 mm of one; capillaries, 0.8 mm across, only within 2.5 mm of a branch. Nothing is narrower than about two of
// the light's source cells (0.48 mm at the default LightCells), since the light reads its pigment from those cells. The
// levels sit in separate bands of one value, so that one colour_map gives each its own colour and strength, and a thicker
// vessel takes a finer one's place.
// a trunk's half-width: 1.2 mm at the wrist, 0.8 mm past the knuckles
#declare TrunkWidth=function(vX) { 0.0008+0.0004*min(1,max(0,(vX+0.04)/0.15)) };
// every vessel tapers to nothing over the last 25 mm before the layer ends at the fingers and the wrist
#declare Fade=function(vX) { min(1,max(0,(vX+0.068)/0.025))*min(1,max(0,(0.123-vX)/0.025)) };
#declare VeinBands=function(vX,vDT,vDB,vDC) {
 max(select(Line(vDT,TrunkWidth(vX))-1e-6, 0, 0.67+0.33*Line(vDT,TrunkWidth(vX))),
     select(Line(vDB,0.0007*(1-0.4*min(1,vDT/0.007)))*(vDT<0.007)-1e-6, 0, 0.34+0.32*Line(vDB,0.0007*(1-0.4*min(1,vDT/0.007)))),
     0.33*Line(vDC,0.0004)*(vDB<0.0025)*(vDT<0.008))
};
#declare VeinPigment=pigment { function { VeinBands(x,TrunkAway(x,z)+0.0015*(1-Fade(x)),BranchAway(x,z)+0.0008*(1-Fade(x)),CapillaryAway(x,z)+0.0005*(1-Fade(x))) }
 colour_map {
  [0.00 rgb 0] [0.33 rgb <0.25,1.0,0.80>*CapillaryGain]
  [0.335 rgb 0] [0.66 rgb <0.22,0.70,1.0>*BranchGain]
  [0.665 rgb 0] [1.00 rgb <0.30,0.45,1.0>]
 }
};
// the layer, cut to the back of the hand and the fingers' first joints so that the light's cells stay small
#declare VeinLayer=intersection { object { HandShape(0.0003) } object { HandShape(0.0012) inverse } box { <-0.07,0.010,-0.058>,<0.125,0.036,0.042> } };

union {
 object { HandObj texture { SkinTex } interior { ior 1.4 } subsurface { domain_cells Cells } }
 // nails
 #for (J,0,3)
  #local N=vnormalize(FJ[J][3]-FJ[J][2]);
  sphere { 0,1 scale <0.0055,0.0011,FR[J][2]*0.62> rotate z*-degrees(asin(N.y)) rotate y*(FS[J]) translate FJ[J][2]+(FJ[J][3]-FJ[J][2])*0.78+y*FR[J][2]*0.80
   pigment { rgb <0.86,0.64,0.58> } finish { ambient 0 diffuse 0.6 specular 0.5 roughness 0.01 } }
 #end
 // the buried light: one volume emitter shaped by the vein pigment
 object { VeinLayer
  no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { pigment { VeinPigment } brightness VeinNits samples Samples domain_cells LightCells attenuation { cutoff 0 } } }
 }
 // sleeve over the wrist
 difference {
  cylinder { <0.135,0.026,0.004>,<0.32,0.030,0.006>,0.036 }
  cylinder { <0.12,0.026,0.004>,<0.33,0.030,0.006>,0.031 }
  scale <1,0.85,1>
  pigment { rgb <0.10,0.11,0.14> } normal { average normal_map { [1 wrinkles 0.5 scale <0.03,0.006,0.006>] [1 bumps 0.2 scale 0.0004] } }
  finish { ambient 0 diffuse 0.85 specular 0.05 roughness 0.1 }
 }
 rotate y*-12
}
