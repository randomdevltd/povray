// Subsurface showcase: one night-time desk lit mostly by its own glowing objects.
// Units are metres; subsurface distances are millimetres (mm_per_unit 1000). See showcase.md.
#version 4.0;
#include "functions.inc"
#ifndef (View) #declare View=0; #end
#ifndef (Exposure) #declare Exposure=0.075; #end
#ifndef (Fill) #declare Fill=1; #end
#ifndef (SssSamples) #declare SssSamples=32; #end
#ifndef (SssInternal) #declare SssInternal=64; #end
#ifndef (LightSamples) #declare LightSamples=32; #end
#ifndef (Export) #declare Export=1; #end
#ifndef (Normals) #declare Normals=1; #end
#ifndef (Pigments) #declare Pigments=1; #end
#ifndef (Sources) #declare Sources=1; #end
// physical source fluxes, real lumens before camera exposure
#ifndef (CandleLumens) #declare CandleLumens=12; #end
#ifndef (LanternLumens) #declare LanternLumens=6; #end
#ifndef (EggLumens) #declare EggLumens=1.2; #end
#ifndef (HandLumens) #declare HandLumens=0.1; #end
#ifndef (PanelLumens) #declare PanelLumens=0.3; #end
#ifndef (RuneLumens) #declare RuneLumens=0.1; #end
#declare Lm=Exposure*Sources;

global_settings { assumed_gamma 1 mm_per_unit 1000 max_trace_level 16 prepared_media_cells 24000000
 subsurface { method 2 samples SssSamples,SssInternal spacing 1 error_bound 0.15 } }
#default { finish { ambient 0 diffuse 0.7 } }

// ---------------------------------------------------------------- cameras
#switch (View)
#case (0) camera { location <0.02,0.30,-0.60> look_at <0.02,0.06,0.08> angle 42 right x*image_width/image_height } #break
#case (1) camera { location <0.12,0.15,-0.22> look_at <0.0,0.05,0.04> angle 36 right x*image_width/image_height } #break // egg
#case (2) camera { location <0.20,0.20,-0.38> look_at <0.20,0.015,-0.07> angle 36 right x*image_width/image_height } #break // hand
#case (3) camera { location <0.07,0.13,-0.10> look_at <0.13,0.09,0.17> angle 34 right x*image_width/image_height } #break // ear
#case (4) camera { location <-0.08,0.15,-0.12> look_at <-0.12,0.08,0.16> angle 40 right x*image_width/image_height } #break // candle
#case (5) camera { location <0.10,0.18,-0.12> look_at <0.20,0.10,0.26> angle 40 right x*image_width/image_height } #break // lantern
#case (6) camera { location <0.17,0.12,-0.12> look_at <0.21,0.02,0.07> angle 30 right x*image_width/image_height } #break // grapes
#case (7) camera { location <-0.17,0.15,-0.05> look_at <-0.21,0.06,0.24> angle 36 right x*image_width/image_height } #break // panel
#case (8) camera { location <-0.10,0.22,-0.30> look_at <-0.12,0.0,-0.07> angle 38 right x*image_width/image_height } #break // scroll
#end

// ---------------------------------------------------------------- room
light_source { <-0.9,0.9,-0.6> rgb <1,0.62,0.34>*0.06*Fill area_light x*0.3,y*0.3,4,4 adaptive 1 jitter tags { "fill" } }
plane { y,0
 texture {
  pigment { wood turbulence 0.08 octaves 3 scale <0.02,0.02,1> rotate y*88 warp { turbulence <0.03,0.0,0.08> octaves 4 }
   colour_map { [0 rgb <0.30,0.14,0.055>] [0.35 rgb <0.22,0.095,0.035>] [0.7 rgb <0.36,0.18,0.07>] [1 rgb <0.26,0.12,0.045>] } }
  normal { average normal_map { [1 wood 0.03 scale <0.02,0.02,1> rotate y*88] [1 bumps 0.03 scale 0.0008] } }
  finish { diffuse 0.75 specular 0.08 roughness 0.04 reflection { 0.0 0.02 } }
 }
 tags { "room" }
}
plane { -z,-0.42
 pigment { granite turbulence 0.3 scale 0.08 colour_map { [0 rgb <0.36,0.22,0.13>] [0.6 rgb <0.30,0.18,0.10>] [1 rgb <0.40,0.26,0.15>] } }
 normal { granite 0.12 scale 0.02 } finish { diffuse 0.8 }
 tags { "room" }
}

// ---------------------------------------------------------------- shared textures
#declare Brass=texture { pigment { rgb <0.55,0.36,0.13> } normal { bumps 0.05 scale 0.0004 } finish { diffuse 0.25 metallic specular 0.9 roughness 0.012 reflection { 0.25 metallic } } };
#declare DarkIron=texture { pigment { rgb <0.05,0.04,0.035> } normal { granite 0.15 scale 0.002 } finish { diffuse 0.5 specular 0.4 roughness 0.03 } };
#macro Sss(Base,Dist)
 finish { diffuse 0.75 specular 0.25 roughness 0.04 subsurface { method 2 translucency Dist colour rgb 0.95 thickness 0.4 } }
#end
// calibrated flame: spherical density, visible emission and cast light share one flux
#macro Flame(Axes,Lumens)
 #local FlameColour=colour rgb <3,1.07143,0.17143>;
 #local FlameVolume=4*pi*Axes.x*Axes.y*Axes.z/3;
 sphere { 0,1 pigment { rgbt 1 }
  interior { media { mix add method 4 resolution 0.001 emission FlameColour*Lumens*Lm/(pi*FlameVolume*FlameColour.gray) density { spherical } }
   #if (Sources) light { emission brightness Lumens*Lm lumens samples LightSamples } #end }
  scale Axes
 }
#end

// ================================================================ dragon egg
// two half-ellipsoids give an ovoid; Inset shrinks it by a uniform depth
#macro EggShape(Inset)
 merge {
  intersection { sphere { 0,1 scale <0.032-Inset,0.037-Inset,0.032-Inset> } plane { y,0 } }
  intersection { sphere { 0,1 scale <0.032-Inset,0.050-Inset,0.032-Inset> } plane { -y,0 } }
 }
#end
// shingled scales: per-row ellipse height, the row above lies on top where it overlaps
#declare ScaleRows=0.0062; #declare ScaleCount=22;
#declare ScaleCell=function(A,Y,Row) {
 sqrt(pow((A*ScaleCount/(2*pi)+0.5*mod(Row+100,2)-floor(A*ScaleCount/(2*pi)+0.5*mod(Row+100,2))-0.5)/0.60,2)
     +pow((Y/ScaleRows-Row-0.32)/0.74,2)) };
#declare ScaleOwn=function(A,Y) { 1-ScaleCell(A,Y,floor(Y/ScaleRows)) };
#declare ScaleUp=function(A,Y) { 1-ScaleCell(A,Y,floor(Y/ScaleRows)+1) };
#declare ScaleH=function(x,y,z) { max(0,select(ScaleUp(atan2(x,z),y),ScaleOwn(atan2(x,z),y),ScaleUp(atan2(x,z),y))) };
#macro EggTexture()
 texture { pigment { rgb <0.85,0.30,0.08> } Sss(0,<6,2.4,1.0>) }
 #if (Pigments)
 texture {
  pigment { function { ScaleH(x,y,z) }
   colour_map { [0 rgbt <0.9,0.35,0.05,1>] [0.03 rgbt <0.8,0.3,0.05,0.8>] [0.10 rgbt <0.35,0.10,0.03,0.35>] [0.18 rgb <0.10,0.035,0.02>] [0.5 rgb <0.17,0.07,0.035>] [1 rgb <0.26,0.13,0.05>] } }
  #if (Normals) normal { function { ScaleH(x,y,z) } 0.9 accuracy 0.00015 } #end
  finish { diffuse 0.6 specular 0.7 roughness 0.012 metallic 0.6 }
 }
 #end
#end
union {
 union {
 object { EggShape(0) EggTexture() interior { ior 1.45 }
  #if (Export) light { emission samples LightSamples } #end
 }
 #if (Sources)
 difference { EggShape(0.0022) EggShape(0.0080)
  pigment { rgbt 1 }
  interior { ior off media { mix add method 4 resolution 0.0005 emission <1,0.42,0.07>
    density { granite turbulence 0.4 scale <0.012,0.03,0.012> colour_map { [0 rgb 0.25] [0.5 rgb 1] [1 rgb 0.4] } } }
   light { emission brightness EggLumens*Lm lumens samples LightSamples domain_cells 400000 } }
 }
 #end
 translate y*(0.0125+0.0302)
 }
 // iron ring stand
 torus { 0.021,0.0025 translate y*0.010 texture { DarkIron } }
 #for (J,0,2) cylinder { <0.021*sin(J*2*pi/3),0.010,0.021*cos(J*2*pi/3)>,<0.027*sin(J*2*pi/3),0.0018,0.027*cos(J*2*pi/3)>,0.0018 texture { DarkIron } } #end
 translate <0.0,0,0.04>
 tags { "egg" }
}

// ================================================================ hand (right hand, palm down, fingers toward -x)
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
// CSG of linear sphere sweeps; I insets every surface for the buried capillary layer
#macro HandShape(I)
 merge {
  sphere { 0,1 scale <0.050-I,0.0125-I,0.040-I> translate <0.004,0.0135,0> }
  sphere_sweep { linear_spline 2, <-0.034,0.017,-0.027>,0.0095-I, <-0.034,0.016,0.026>,0.0085-I }
  sphere { 0,1 scale <0.030-I,0.0115-I,0.017-I> translate <0.018,0.0125,-0.026> }
  sphere_sweep { linear_spline 2, <0.050,0.022,0.0>,0.0205-I, <0.17,0.025,0.004>,0.020-I }
  #for (J,0,3)
   sphere_sweep { linear_spline 5, <0.030,0.019,FZ[J]*0.6>,0.0075-I, FJ[J][0],0.0095-I, FJ[J][1],FR[J][1]*1.04-I, FJ[J][2],FR[J][2]*1.0-I, FJ[J][3],FR[J][2]*0.88-I }
  #end
  sphere_sweep { linear_spline 4, <0.040,0.014,-0.022>,0.0125-I, <0.005,0.013,-0.045>,0.0108-I, <-0.025,0.012,-0.060>,0.0094-I, <-0.046,0.010,-0.066>,0.0082-I }
 }
#end
#declare SkinTex=texture {
 pigment { granite turbulence 0.3 scale 0.02 colour_map { [0 rgb <0.80,0.50,0.38>] [0.5 rgb <0.76,0.45,0.34>] [1 rgb <0.82,0.52,0.40>] } }
 #if (Normals) normal { average normal_map {
   [1 bumps 0.35 scale 0.00028]
   [1 wrinkles 0.45 scale <0.0012,0.004,0.010>]
   [0.6 granite 0.25 scale 0.0015] } } #end
 finish { diffuse 0.7 specular 0.22 roughness 0.035 subsurface { method 2 translucency <2.2,1.0,0.55> colour rgb 0.95 thickness 0.4 } }
};
#declare CapillaryDensity=density {
 crackle form <-1,1,0> metric 2 turbulence 0.6 octaves 5 omega 0.58 lambda 2.2 scale <0.018,1000,0.006>
 colour_map { [0 rgb 1] [0.025 rgb 1] [0.07 rgb 0] [1 rgb 0] }
};
union {
 object { HandShape(0) texture { SkinTex } interior { ior 1.4 }
  #if (Export) light { emission samples LightSamples } #end
 }
 // nails
 #for (J,0,3)
  #local N=vnormalize(FJ[J][3]-FJ[J][2]);
  sphere { 0,1 scale <0.0055,0.0011,FR[J][2]*0.62> rotate z*-degrees(asin(N.y)) rotate y*(FS[J]) translate FJ[J][2]+(FJ[J][3]-FJ[J][2])*0.78+y*FR[J][2]*0.80
   pigment { rgb <0.86,0.62,0.55> } finish { diffuse 0.6 specular 0.5 roughness 0.01 } }
 #end
 #if (Sources)
 intersection { HandShape(0.0004) object { HandShape(0.0011) inverse } box { <-0.14,-0.001,-0.08>,<0.06,0.036,0.04> }
  pigment { rgbt 1 }
  interior { ior off media { mix add method 4 resolution 0.0005 emission <0.45,0.35,1.0> density { CapillaryDensity } }
   light { emission brightness HandLumens*Lm lumens samples LightSamples domain_cells 2000000 } }
 }
 #end
 // sleeve over the wrist
 difference {
  cylinder { <0.135,0.026,0.004>,<0.32,0.030,0.006>,0.036 }
  cylinder { <0.12,0.026,0.004>,<0.33,0.030,0.006>,0.031 }
  scale <1,0.85,1>
  pigment { rgb <0.20,0.035,0.03> } normal { average normal_map { [1 wrinkles 0.5 scale <0.03,0.006,0.006>] [1 bumps 0.2 scale 0.0004] } }
  finish { diffuse 0.85 specular 0.05 roughness 0.1 }
 }
 rotate y*-18
 translate <0.20,0,-0.085>
 tags { "hand" }
}

// ================================================================ ear specimen on a brass pin (pointed, backlit by the lantern)
#macro EarShape()
 difference {
  merge {
   sphere { 0,1 scale <0.0165,0.026,0.0024> translate <0.003,0.005,0.0015> }
   sphere_sweep { linear_spline 16,
    #for (J,0,15)
     #local A=radians(-70+J*250/15);
     #local P=<0.016*cos(A)+0.002,0.027*sin(A)+0.005,0.0005>;
     #if (J=8) #local P=P+<0.005,0.013,0.001>; #end
     #if (J=7 | J=9) #local P=P+<0.002,0.005,0>; #end
     P,(J=8?0.0018:0.0032-0.001*abs(J-6)/9),
    #end
   }
   sphere_sweep { linear_spline 3, <0.002,-0.016,-0.0015>,0.0024, <0.008,0.003,-0.0015>,0.0026, <0.004,0.020,-0.0005>,0.0018 }
   sphere_sweep { linear_spline 2, <0.008,0.003,-0.0015>,0.0022, <-0.006,0.015,-0.0005>,0.0016 }
   sphere { <-0.0125,-0.009,-0.002>,0.0042 }
   sphere { <0.003,-0.019,-0.0018>,0.003 }
   sphere { 0,1 scale <0.0105,0.0105,0.0040> translate <-0.002,-0.030,0.0005> }
  }
  sphere { 0,1 scale <0.0085,0.010,0.004> translate <-0.0045,-0.007,-0.0035> }
 }
#end
union {
 object { EarShape()
  texture {
   pigment { rgb <0.82,0.50,0.40> }
   #if (Normals) normal { average normal_map { [1 bumps 0.3 scale 0.00025] [0.5 wrinkles 0.25 scale 0.002] } } #end
   finish { diffuse 0.7 specular 0.2 roughness 0.04 subsurface { method 2 translucency <2.6,1.1,0.6> colour rgb 0.95 thickness 0.4 } }
  }
  interior { ior 1.4 }
  #if (Export) light { bounce samples LightSamples } #end
 }
 cylinder { <-0.002,-0.075,0.004>,<-0.002,-0.020,0.004>,0.0012 texture { Brass } }
 cylinder { <-0.002,-0.088,0.004>,<-0.002,-0.081,0.004>,0.016 texture { DarkIron } }
 cylinder { <-0.002,-0.081,0.004>,<-0.002,-0.076,0.004>,0.009 texture { Brass } }
 rotate y*20
 translate <0.115,0.088,0.165>
 tags { "ear" }
}

// ================================================================ wax candle in a brass chamberstick
#declare CandleH=0.105; #declare CandleR=0.0125;
#declare WaxTex=texture {
 pigment { granite turbulence 0.2 scale 0.03 colour_map { [0 rgb <0.96,0.86,0.66>] [1 rgb <0.92,0.80,0.58>] } }
 #if (Normals) normal { average normal_map { [1 wrinkles 0.25 scale <0.004,0.02,0.004>] [1 bumps 0.08 scale 0.0006] } } #end
 finish { diffuse 0.75 specular 0.35 roughness 0.02 subsurface { method 2 translucency <12,8,4.5> colour rgb 0.95 thickness 0.4 } }
};
union {
 merge {
  difference { cylinder { <0,0.012,0>,<0,0.012+CandleH,0>,CandleR } sphere { <0,0.012+CandleH+0.011,0>,0.0145 } }
  #local S=seed(11);
  #for (J,0,8)
   #local Th=J*41+rand(S)*25; #local L=0.006+0.028*rand(S);
   #local P=<sin(radians(Th))*(CandleR-0.0006),0.012+CandleH-0.0012,cos(radians(Th))*(CandleR-0.0006)>;
   sphere_sweep { linear_spline 3, P,0.0014, P-y*L*0.6+<P.x,0,P.z>*0.02,0.0013, P-y*L+<P.x,0,P.z>*0.03,0.0017 }
  #end
  #for (J,0,5) #local Th=J*60+rand(S)*30; sphere { <sin(radians(Th))*CandleR*0.9,0.012+CandleH-0.0012,cos(radians(Th))*CandleR*0.9>,0.0019 } #end
  texture { WaxTex } interior { ior 1.45 }
  #if (Export) light { bounce samples LightSamples } #end
 }
 // wick
 sphere_sweep { linear_spline 3, <0,0.012+CandleH-0.004,0>,0.0006, <0,0.012+CandleH+0.004,0>,0.0006, <0.0012,0.012+CandleH+0.009,0>,0.0005 pigment { rgb 0.02 } }
 object { Flame(<0.0045,0.014,0.0045>,CandleLumens) translate y*(0.012+CandleH+0.014) }
 // chamberstick
 lathe { linear_spline 7, <0,0>,<0.040,0>,<0.044,0.004>,<0.042,0.007>,<0.016,0.008>,<0.016,0.014>,<0,0.014> texture { Brass } }
 torus { 0.042,0.0022 translate y*0.0055 texture { Brass } }
 difference { torus { 0.010,0.0022 rotate x*90 } box { <-1,-1,-1>,<1,0,1> } translate <0.051,0.004,0> texture { Brass } }
 translate <-0.12,0,0.16>
 tags { "candle" }
}

// ================================================================ membrane lantern
#declare LanR=0.052; #declare LanH=0.070; #declare LanT=0.0012;
union {
 difference {
  sphere { 0,1 scale <LanR,LanH,LanR> }
  sphere { 0,1 scale <LanR-LanT,LanH-LanT,LanR-LanT> }
  cylinder { <0,LanH*0.84,0>,<0,LanH*1.2,0>,0.020 }
  cylinder { <0,-LanH*0.84,0>,<0,-LanH*1.2,0>,0.024 }
  texture { pigment { granite turbulence 0.5 scale <0.02,0.05,0.02> colour_map { [0 rgb <0.95,0.74,0.46>] [1 rgb <0.90,0.66,0.38>] } }
   #if (Normals) normal { wrinkles 0.2 scale <0.001,0.008,0.001> } #end
   finish { diffuse 0.75 specular 0.12 roughness 0.05 subsurface { method 2 translucency <3,2,1.1> colour rgb 0.95 thickness 0.4 } } }
  #if (Pigments)
  // painted vine lacquer: opaque strokes, open membrane between them
  texture { pigment { marble turbulence 1.1 octaves 5 scale <0.03,0.06,0.03> colour_map { [0 rgb <0.30,0.05,0.02>] [0.06 rgb <0.30,0.05,0.02>] [0.11 rgbt <1,1,1,1>] [0.88 rgbt <1,1,1,1>] [0.92 rgb <0.12,0.03,0.015>] [1 rgb <0.12,0.03,0.015>] } }
   finish { diffuse 0.6 specular 0.3 roughness 0.03 } }
  #end
  interior { ior 1.45 }
  #if (Export) light { bounce samples LightSamples } #end
 }
 // brass ribs, cap, handle and base
 #for (J,0,7)
  intersection { torus { 1,0.012 rotate x*90 scale <LanR+0.0008,LanH+0.0008,LanR+0.0008> } box { <-1,-LanH*0.86,-0.01>,<1,LanH*0.86,0.01> } rotate y*J*45 texture { Brass } }
 #end
 cylinder { <0,LanH*0.82,0>,<0,LanH*0.98,0>,0.022 open texture { Brass } }
 torus { 0.022,0.0016 translate y*LanH*0.98 texture { Brass } }
 torus { 0.016,0.0014 rotate x*90 translate y*(LanH+0.020) texture { Brass } }
 cylinder { <0,-LanH*0.82,0>,<0,-LanH*0.95,0>,0.026 texture { Brass } }
 cylinder { <0,-LanH*0.95,0>,<0,-LanH-0.016,0>,0.034 texture { Brass } }
 torus { 0.034,0.002 translate y*(-LanH-0.016) texture { Brass } }
 // oil flame inside
 object { Flame(<0.006,0.016,0.006>,LanternLumens) translate y*-0.02 }
 translate <0.205,LanH+0.016,0.27>
 tags { "lantern" }
}

// ================================================================ grapes
#macro GrapeTex()
 texture { pigment { granite turbulence 0.3 scale 0.01 colour_map { [0 rgb <0.62,0.66,0.16>] [1 rgb <0.72,0.64,0.20>] } }
  #if (Normals) normal { bumps 0.06 scale 0.0015 } #end
  finish { diffuse 0.7 specular 0.55 roughness 0.012 subsurface { method 2 translucency <9,8,2.4> colour rgb 0.95 thickness 0.4 } } }
 #if (Pigments)
 texture { pigment { granite scale 0.0007 colour_map { [0 rgbt <0.85,0.88,0.80,0.55>] [0.5 rgbt <1,1,1,1>] [1 rgbt <1,1,1,1>] } } finish { diffuse 0.8 specular 0 } }
 #end
#end
union {
 #local S=seed(5);
 #local GP=array[13] {<0,0.010,0>,<0.019,0.010,0.004>,<-0.018,0.010,0.006>,<0.008,0.010,0.020>,<-0.008,0.010,-0.016>,<0.012,0.010,-0.018>,
   <-0.026,0.010,-0.010>,<0.028,0.010,-0.012>,<-0.010,0.010,0.025>,<0.003,0.026,0.004>,<-0.012,0.025,-0.002>,<0.014,0.025,0.000>,<0.002,0.024,-0.013>};
 #for (J,0,12)
  #local P=GP[J]+<rand(S)-0.5,0,rand(S)-0.5>*0.002;
  #local R=0.0095+rand(S)*0.0015;
  sphere { 0,1 scale <R,R*1.12,R> rotate <rand(S)*360,0,rand(S)*360> translate P GrapeTex() interior { ior 1.35 } }
  cylinder { P+y*R*0.9,P+<0,R+0.006,0>,0.0007 pigment { rgb <0.25,0.20,0.07> } }
 #end
 sphere_sweep { linear_spline 4, <0.002,0.031,0.0>,0.0012, <0.004,0.034,0.004>,0.0012, <0.012,0.038,0.012>,0.0011, <0.024,0.040,0.020>,0.001 pigment { rgb <0.28,0.22,0.08> } }
 translate <0.215,0,0.065>
 tags { "grapes" }
}

// ================================================================ alien circuit tablet leaning on two books
#declare PanelHD=0.004;
#declare CS=seed(23);
#declare PadEnds=array[16];
#declare Traces=union {
 #for (I,0,15)
  #local P=<int((rand(CS)-0.5)*22)*0.0025,int((rand(CS)-0.5)*30)*0.0025,0>;
  #local Dir=int(rand(CS)*8);
  #for (St,0,5)
   #local Q=P+(0.003+int(rand(CS)*4)*0.0025)*<cos(Dir*pi/4),sin(Dir*pi/4),0>;
   #local Q=<min(0.028,max(-0.028,Q.x)),min(0.038,max(-0.038,Q.y)),0>;
   #if (vlength(Q-P)>0.0005) cylinder { P,Q,0.00035 } sphere { Q,0.00035 } #end
   #local P=Q; #local Dir=mod(Dir+int(rand(CS)*3)-1+8,8);
  #end
  cylinder { P-z*0.00035,P+z*0.00035,0.0010 }
  #declare PadEnds[I]=P;
 #end
};
#declare TraceField=function { pattern { object { Traces } } };
#declare Pads=union { #for (I,0,15,3) cylinder { PadEnds[I]-z*0.01,PadEnds[I]+z*0.01,0.0017 } #end };
union {
 superellipsoid { <0.12,0.12> scale <0.034,0.044,PanelHD>
  texture { pigment { granite turbulence 0.4 scale 0.015 colour_map { [0 rgb <0.92,0.82,0.58>] [1 rgb <0.86,0.74,0.50>] } }
   #if (Normals) normal { average normal_map { [1 bumps 0.15 scale 0.0003] [1 granite 0.1 scale 0.004] } } #end
   finish { diffuse 0.7 specular 0.45 roughness 0.015 subsurface { method 2 translucency <5,4,3> colour rgb 0.95 thickness 0.4 } } }
  #if (Pigments)
  texture { pigment { object { Pads rgbt 1 rgb <0.62,0.42,0.12> } } finish { diffuse 0.3 metallic specular 0.8 roughness 0.01 } }
  #end
  interior { ior 1.5 }
  #if (Export) light { emission samples LightSamples } #end
 }
 #if (Sources)
 box { <-0.031,-0.041,-0.00045>,<0.031,0.041,0.00045>
  pigment { rgbt 1 }
  interior { ior off media { mix add method 4 resolution 0.00025 emission <0.15,0.85,1.0> density { function { TraceField(x,y,z) } } }
   light { emission brightness PanelLumens*Lm lumens samples LightSamples domain_cells 1000000 } }
  translate z*(-PanelHD+0.0014)
 }
 #end
 // brass easel prop behind the tablet
 cylinder { <0,0,PanelHD>,<0,-0.05,0.028> ,0.0012 texture { Brass } }
 rotate x*16
 translate <0,0.044*cos(radians(16))+0.035,0>
 rotate y*-22
 translate <-0.215,0,0.24>
 tags { "panel" }
}
// two old books under and behind the tablet
union {
 box { <-0.07,0,-0.05>,<0.07,0.021,0.05> pigment { rgb <0.32,0.07,0.04> } normal { bumps 0.2 scale 0.001 } finish { diffuse 0.7 specular 0.1 roughness 0.05 } }
 box { <-0.067,0.0215,-0.047>,<0.064,0.033,0.046> pigment { rgb <0.10,0.16,0.08> } normal { bumps 0.2 scale 0.001 } finish { diffuse 0.7 specular 0.1 roughness 0.05 } rotate y*5 }
 box { <-0.066,0.001,-0.0485>,<0.069,0.020,0.0485> pigment { rgb <0.85,0.78,0.6> } normal { wrinkles 0.4 scale <0.0005,0.0005,1> } }
 rotate y*-22 translate <-0.215,0,0.26>
 tags { "panel" }
}

// ================================================================ paper scroll with runes
// one CSG solid: flat run, a lifted curl at the right and a two-centre spiral roll at the left
#declare PaperT=0.00035; #declare PaperW=0.085; #declare FlatL=0.16;
#declare RollR=0.0085; #declare RollP=0.0014; #declare RollHalfTurns=6; #declare CurlR=0.03; #declare CurlA=55;
#macro Shell(C,R) difference { cylinder { C,C+z*PaperW,R+PaperT/2 } cylinder { C-z,C+z*(PaperW+1),R-PaperT/2 } } #end
#declare Paper=merge {
 box { <0,0,0>,<FlatL,PaperT,PaperW> }
 #for (K,0,RollHalfTurns-1)
  #local R=RollR-K*RollP/2;
  #local C=<0,PaperT/2+(mod(K,2)=0?RollR:RollR+RollP/2),0>;
  intersection { Shell(C,R) plane { (mod(K,2)=0?x:-x),0 } }
 #end
 intersection { Shell(<FlatL,PaperT/2+CurlR,0>,CurlR) plane { -x,-FlatL } plane { x,0 rotate z*CurlA translate <FlatL,PaperT/2+CurlR,0> } plane { y,PaperT/2+CurlR } }
};
#macro PaperTex()
 texture { pigment { granite turbulence 0.4 scale 0.02 colour_map { [0 rgb <0.93,0.85,0.66>] [0.6 rgb <0.90,0.80,0.60>] [1 rgb <0.80,0.66,0.44>] } }
  #if (Normals) normal { average normal_map { [1 wrinkles 0.35 scale <0.0004,0.0004,0.004>] [1 bumps 0.2 scale 0.00025] } } #end
  finish { diffuse 0.75 specular 0.05 roughness 0.06 subsurface { method 2 translucency <1.1,0.95,0.7> colour rgb 0.95 thickness 0.4 } } }
#end
#declare RuneLines=array[5] {"<|>  XIV  {#}  ^/\\^","Y|Y  >o<  M#W  :|:","/\\/  <X>  ]+[  W|M","%~%  |-|  <*>  )(X","#^#  Z|Z  {o}  V/V"};
#macro RuneLine(J,Depth,Y0)
 text { ttf "timrom.ttf" RuneLines[J] 1,0 scale <0.0075,0.0075,Depth> rotate x*90 translate <0.012,Y0,0.052-J*0.0115> }
#end
#macro TitleGlyphs(Depth,Y0) text { ttf "timrom.ttf" "X-|<>|-X" 1,0 scale <0.011,0.011,Depth> rotate x*90 translate <0.035,Y0,0.066> } #end
#declare InkTitle=TitleGlyphs(0.003,PaperT+0.001);
union {
 object { Paper
  PaperTex()
  #if (Pigments)
  // ink: the title line is painted opaque over a buried glyph source
  texture { pigment { object { InkTitle rgbt 1 rgb <0.05,0.025,0.02> } } finish { diffuse 0.5 specular 0.2 roughness 0.03 } }
  #end
  interior { ior 1.45 }
  #if (Export) light { emission samples LightSamples } #end
 }
 #if (Sources)
 #for (J,0,4)
  object { RuneLine(J,0.00012,PaperT/2+0.00006) pigment { rgbt 1 }
   interior { ior off media { mix add method 4 resolution 0.0002 emission <1,0.55,0.12> }
    light { emission brightness RuneLumens*Lm/6 lumens samples LightSamples } } }
 #end
 object { TitleGlyphs(0.00012,PaperT/2+0.00006) pigment { rgbt 1 }
  interior { ior off media { mix add method 4 resolution 0.0002 emission <1,0.55,0.12> }
   light { emission brightness RuneLumens*Lm/3 lumens samples LightSamples } } }
 #end
 rotate y*8
 translate <-0.17,0,-0.15>
 tags { "scroll" }
}
