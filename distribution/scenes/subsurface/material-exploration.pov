#version 4.0;
#include "functions.inc"
#ifndef (Treatment) #declare Treatment=2; #end
#ifndef (Artifact) #declare Artifact=-1; #end
#ifndef (Case) #declare Case=-1; #end
#ifndef (Sheet) #declare Sheet=0; #end
#ifndef (SubsurfaceSamples) #declare SubsurfaceSamples=12; #end
#ifndef (InternalSamples) #declare InternalSamples=2; #end
#ifndef (EmitterSamples) #declare EmitterSamples=16; #end
#ifndef (VeinPattern) #declare VeinPattern=0; #end
#ifndef (VeinScale) #declare VeinScale=0.005; #end
#ifndef (VeinLayer) #declare VeinLayer=0; #end
#ifndef (CameraZoom) #declare CameraZoom=1; #end
#ifndef (PanelHalfDepth) #declare PanelHalfDepth=0.006; #end
#ifndef (PanelRound) #declare PanelRound=1; #end
#ifndef (EmissionExport) #declare EmissionExport=0; #end
#ifndef (Export) #declare Export=0; #end
#ifndef (SurfaceSource) #declare SurfaceSource=0; #end
#ifndef (SheetBounce) #declare SheetBounce=0; #end
#ifndef (WaxLength) #declare WaxLength=0.083; #end
#ifndef (WaxRadius) #declare WaxRadius=0.0105; #end
#ifndef (CandleLumens) #declare CandleLumens=12; #end
#ifndef (WaxBounce) #declare WaxBounce=0; #end
#ifndef (WaxBouncePasses) #declare WaxBouncePasses=1; #end
#ifndef (SourceOn) #declare SourceOn=1; #end
#ifndef (NormalStrength) #declare NormalStrength=0.12; #end
#ifndef (InternalSource) #declare InternalSource=0; #end
#ifndef (Fill) #declare Fill=1; #end
#ifndef (TransportScale) #declare TransportScale=-1; #end
#ifndef (LookupDepth) #declare LookupDepth=-1; #end
#ifndef (LookupSpread) #declare LookupSpread=-1; #end
#ifndef (SkinPattern) #declare SkinPattern=0; #end
#ifndef (SourceModeOverride) #declare SourceModeOverride=-1; #end
#ifndef (Exposure) #declare Exposure=1; #end
#if (Case>863 | Case< -1) #error "Case must be -1 or 0 through 863" #end
#if (Artifact>7 | Artifact< -1) #error "Artifact must be -1 or 0 through 7" #end
#if (Treatment<0 | Treatment>4) #error "Treatment must be 0 through 4" #end
#declare SourceMode=1;
#declare CurrentBounce=WaxBounce;
#declare CurrentVolume=1;
#declare CurrentNormal=NormalStrength;
#declare Travel=array[5] {0.35,0.7,1,1.7,2.5};
#declare Depths=array[5] {0.2,0.5,1,2,3};
#declare Treatments=array[5] {"SURFACE / SHORT","SAMPLED / SHORT","EMBEDDED GLOW","EMBEDDED / LONG","LONG / PORES"};
#declare Names=array[8] {"ALIEN VEINS","SKIN / EAR","WAX / FLAME","LAMP MEMBRANE","FRUIT / CORE","LEAF RIBS","ALIEN PANEL","RUNE SCROLL"};
#declare Veins=pigment { marble turbulence 0.9 octaves 6 omega 0.6 lambda 2.2
 colour_map {[0 rgb <0.3,1.35,7.2>*Exposure] [0.04 rgb <0.3,1.35,7.2>*Exposure] [0.10 rgb 0] [1 rgb 0]}
 scale <VeinScale,VeinScale*3.6,VeinScale> };
#declare VeinField=function {pigment {Veins}};
#declare RuneX=function(X) {mod(X+0.036,0.018)-0.009};
#declare RuneY=function(Y) {mod(Y+0.036,0.018)-0.009};
#declare RuneMask=function(X,Y) {max(0,1-min(min(abs(RuneX(X)-0.005),abs(RuneX(X)+0.005)),min(abs(RuneY(Y)),abs(RuneY(Y)-RuneX(X))))/0.0007)};
#declare CircuitX=function(X) {mod(X+0.0285,0.019)-0.0095};
#declare CircuitMask=function(X,Y) {max(0,1-min(abs(CircuitX(X)),abs(Y-0.0056))/0.0006)};
#declare SegmentT=function(X,Y,AX,AY,BX,BY) {min(1,max(0,((X-AX)*(BX-AX)+(Y-AY)*(BY-AY))/(pow(BX-AX,2)+pow(BY-AY,2))))};
#declare SegmentDistance=function(X,Y,AX,AY,BX,BY) {sqrt(pow(X-AX-(BX-AX)*SegmentT(X,Y,AX,AY,BX,BY),2)+pow(Y-AY-(BY-AY)*SegmentT(X,Y,AX,AY,BX,BY),2))};
#declare VascularField=function(X,Y) {max(0,1-min(
 SegmentDistance(X,Y,0,-0.045,0,0.002),
 SegmentDistance(X,Y,0,0.002,-0.018,0.008),SegmentDistance(X,Y,-0.018,0.008,-0.0225,0.044),
 SegmentDistance(X,Y,0,0.002,-0.006,0.008),SegmentDistance(X,Y,-0.006,0.008,-0.0075,0.052),
 SegmentDistance(X,Y,0,0.002,0.006,0.008),SegmentDistance(X,Y,0.006,0.008,0.0075,0.052),
 SegmentDistance(X,Y,0,0.002,0.018,0.008),SegmentDistance(X,Y,0.018,0.008,0.0225,0.044),
 SegmentDistance(X,Y,0,-0.012,-0.019,-0.012),SegmentDistance(X,Y,-0.019,-0.012,-0.038,0.009)
 )/0.0015)};
#declare Runes=union {
 #for(R,0,3)
  text {ttf "timrom.ttf" "FXH" 1,0 scale <0.015,0.015,0.012> translate <-0.025,R*0.018-0.033,-0.006>}
 #end
}
#declare Circuit=union {
 #for(J,-1,1)
  box {<J*0.019-0.0006,-0.022,-0.006>,<J*0.019+0.0006,0.030,0.006>}
  cylinder {<J*0.019,0.030,-0.006>,<J*0.019,0.030,0.006>,0.003}
 #end
 box {<-0.020,0.005,-0.006>,<0.020,0.0062,0.006>}
}
global_settings { assumed_gamma 1 mm_per_unit 1000 max_trace_level 12
 subsurface {method 2 samples SubsurfaceSamples,InternalSamples spacing 1 error_bound 0.15 memory 256} }
#default {finish {ambient 0 diffuse 0.7}}
background {rgb <0.012,0.019,0.031>}
#declare Single=(Artifact>=0 | Case>=0);
#declare Cols=(Sheet?5:(Single?1:4));
#declare Rows=(Sheet?8:(Single?1:2));
#declare SX=0.12;
#declare SY=(Artifact=2?max(0.17,WaxLength+0.085):0.17);
#declare ViewWidth=max(Cols*SX+0.035,(Rows*SY+0.025)*image_width/image_height);
camera {orthographic location <0,0.07,-0.8> look_at <0,0,0> right x*ViewWidth/CameraZoom up y*ViewWidth*image_height/image_width/CameraZoom}
plane {z,0.07 pigment {rgb <0.026,0.038,0.056>} finish {diffuse 0.7}}
#macro Flesh(A,T)
 #local K=Travel[T]; #local D=Depths[T]; #local Spread=D*0.2;
 #if (Case>=0)
  #local C=int(Case/8); #local K=pow(2,mod(int(C/3),3)-1);
  #local D=0.3*pow(2,mod(int(C/9),3)); #local Spread=D*0.2;
 #end
 #if (TransportScale>=0) #local K=TransportScale; #end
 #if (LookupDepth>=0) #local D=LookupDepth; #end
 #if (LookupSpread>=0) #local Spread=LookupSpread; #end
 #local Base=<0.7,0.38,0.27>;
 #local Dist=<1.8,0.8,0.45>;
 #switch(A)
 #case(0) #local Base=<0.18,0.12,0.25>; #local Dist=<1,1.6,2.8>; #break
 #case(2) #local Base=<0.95,0.90,0.8>; #local Dist=<8,5,3>; #break
 #case(3) #local Base=<0.8,0.67,0.42>; #local Dist=<4,2.5,1.4>; #break
 #case(4) #local Base=<0.95,0.42,0.10>; #local Dist=<5,2,0.6>; #break
 #case(5) #local Base=<0.23,0.52,0.12>; #local Dist=<0.7,1.7,0.3>; #break
 #case(6) #local Base=<0.35,0.72,0.62>; #local Dist=<3,6,4>; #break
 #case(7) #local Base=<0.85,0.91,0.95>; #local Dist=<1.2,1,0.7>; #break
 #end
 texture {
  pigment {#if(SkinPattern) crackle metric 2 turbulence 0.2 colour_map {[0 rgb Base*0.35] [0.12 rgb Base*0.35] [0.2 rgb Base] [1 rgb Base]} scale 0.002 #else rgb Base #end}
  normal {bumps CurrentNormal scale 0.0005}
  finish {diffuse 0.7 specular 0.3 roughness 0.045
   subsurface {method 2 translucency Dist*K colour rgb 0.95 thickness 0.4
    #if (A=0 & SourceMode=1 & !InternalSource) emission pigment {Veins} #end
    #if (A=6 & SourceMode=1 & !InternalSource) emission pigment {object {Circuit rgb 0 rgb <0.05,1,1.7>*Exposure*3}} #end
    #if (A=7 & SourceMode=1 & !InternalSource) emission pigment {object {Runes rgb 0 rgb <1.8,0.32,0.04>*Exposure*3}} #end
    #if (CurrentVolume & !InternalSource) volume_sampling {depth D spread Spread} #end
   }
  }
 }
 interior {ior 1.4}
 #local BounceEnabled=(Export | (CurrentBounce & (A=2 | Case>=0)));
 #if (EmissionExport | BounceEnabled | SourceMode=2 | SurfaceSource)
  light {samples EmitterSamples
   #if (SourceMode=2 | SurfaceSource) pigment {Veins} brightness Exposure*0.05 lumens
   #elseif (EmissionExport) emission #end
   #if (BounceEnabled) bounce {passes WaxBouncePasses} #end
  }
 #end
#end
#macro HandBoundary(Inset)
 merge {
  sphere {0,1 scale <0.025-Inset,0.028-Inset,0.009-Inset> translate y*-0.006}
  cone {<0,-0.026,0>,0.015-Inset,<0,-0.05,0>,0.011-Inset}
  #for(J,0,3) sphere_sweep {linear_spline 2 <(J-1.5)*0.012,0.008,0>,0.0055-Inset, <(J-1.5)*0.015,0.044+0.009*sin(J*pi/3),0>,0.0045-Inset} #end
  sphere_sweep {linear_spline 2 <-0.019,-0.012,0>,0.007-Inset, <-0.038,0.009,0>,0.005-Inset}
 }
#end
#macro Specimen(A,T)
 #switch(A)
 #case(0)
  object {HandBoundary(0) Flesh(A,T)} #break
 #case(1)
  difference {
   merge {torus {0.023,0.008 rotate x*90 scale <0.78,1.4,1>} sphere {0,1 scale <0.018,0.028,0.004> translate <0,-0.005,0.002>} sphere {0,0.009 translate <0,-0.033,0>}}
   sphere {0,1 scale <0.012,0.022,0.008> translate <0.002,0.003,-0.009>}
   Flesh(A,T)
  } #break
 #case(2)
  difference {cylinder {<0,0.040-WaxLength,0>,<0,0.040,0>,WaxRadius} sphere {<0,0.047,0>,0.014} Flesh(A,T)}
  cylinder {<0,0.034,0>,<0,0.047,0>,0.0007 pigment {rgb 0.025}}
  sphere {0,1 scale <0.004,0.013,0.004> translate y*0.058 pigment {rgbt 1}
   interior {media {mix add method 4 resolution 0.001 emission <7,2.5,0.4>*Exposure density {spherical}}
    #if(SourceOn) light {emission brightness (CandleLumens*Exposure) lumens samples EmitterSamples} #end
   }
  }
  cylinder {<0,0.036-WaxLength,0>,<0,0.040-WaxLength,0>,WaxRadius+0.008 pigment {rgb <0.25,0.17,0.065>} finish {specular 0.5 metallic}}
  #break
 #case(3)
  difference {sphere {0,1 scale <0.033,0.046,0.020>} sphere {0,1 scale <0.031,0.044,0.018>} box {<-0.05,-0.06,-0.04>,<0.05,-0.035,0.04>} Flesh(A,T)}
  sphere {0,0.004 pigment {rgb 1} finish {emission Exposure} interior {#if(SourceOn) light {pigment {rgb <1,0.45,0.13>} brightness (0.3*Exposure) lumens samples EmitterSamples} #end}}
  #break
 #case(4)
  difference {sphere {0,1 scale <0.036,0.043,0.028>} box {<-0.05,-0.05,-0.05>,<0.05,0.05,-0.006>} Flesh(A,T)}
  sphere {0,1 scale <0.009,0.032,0.003> translate <0,0,-0.0065> pigment {rgb <0.95,0.83,0.48>}}
  #for(J,-1,1,2) #for(H,-1,1,2) sphere {0,1 scale <0.002,0.004,0.001> translate <J*0.012,H*0.012,-0.007> pigment {rgb <0.15,0.045,0.012>}} #end #end
  #break
 #case(5)
  intersection {sphere {0,1 scale <0.044,0.054,0.002> translate x*0.02} sphere {0,1 scale <0.044,0.054,0.002> translate x*-0.02} Flesh(A,T)}
  cylinder {<0,-0.045,-0.002>,<0,0.045,-0.002>,0.0008 pigment {rgb <0.1,0.22,0.03>}}
  #for(J,-3,3) cylinder {<0,J*0.01,-0.002>,<0.018*(1-abs(J)/5),J*0.01+0.012,-0.002>,0.00035 pigment {rgb <0.1,0.22,0.03>}} #end
  #break
 #case(6)
  #if (PanelRound)
   superellipsoid {<0.18,0.18> scale <0.037,0.045,PanelHalfDepth> Flesh(A,T)}
  #else
   box {<-0.037,-0.045,-PanelHalfDepth>,<0.037,0.045,PanelHalfDepth> Flesh(A,T)}
  #end
  #for(J,-1,1) cylinder {<J*0.019,-0.028,-0.007>,<J*0.019,-0.028,-0.009>,0.005 pigment {rgb <0.08,0.11,0.15>} finish {specular 0.6}} #end
  #break
 #case(7)
  box {<-0.034,-0.041,-0.001>,<0.034,0.041,0.001> Flesh(A,T)}
  #for(J,-1,1,2) cylinder {<-0.038,J*0.043,0>,<0.038,J*0.043,0>,0.004 Flesh(A,T)} #end
  #break
 #end
 #if (InternalSource & SourceMode=1 & (A=0 | A=6 | A=7))
  #local Glow=(A=7?<1.8,0.32,0.04>:<0.05,0.4,1.7>);
  #if (A=0)
   #if (VeinLayer)
    difference {HandBoundary(0.0007) HandBoundary(0.0015)
   #else
    intersection {HandBoundary(0.0007) box {<-0.06,-0.06,-0.0002>,<0.06,0.07,0.0002>}
   #end
  #else
   box {<-0.025,-0.034,-0.0002>,<0.025,0.035,0.0002>
  #end
  pigment {rgbt 1}
   interior {ior off media {mix add method 4 resolution 0.001 emission Glow*Exposure*100
    density {
     #if(A=0)
      #if(VeinPattern) function {VascularField(x,y)} #else function {VeinField(x,y,z).gray} #end
      colour_map {[0 rgb 0] [1 rgb 1]}
     #elseif(A=6) function {CircuitMask(x,y)} colour_map {[0 rgb 0] [1 rgb 1]}
     #else function {RuneMask(x,y)} colour_map {[0 rgb 0] [1 rgb 1]} #end
    }
   }
   light {emission brightness 0.02*Exposure lumens samples EmitterSamples}
   }
  }
 #end

#end
#if (Sheet)
 #for(J,0,4) text {ttf "timrom.ttf" Treatments[J] 0.0001,0 scale 0.004 translate <(J-2)*SX-0.045,Rows*SY/2-0.015,-0.01> pigment {rgb <0.6,0.72,0.8>} finish {emission 0.6*Exposure}} #end
#end
#declare Count=(Sheet?40:(Single?1:8));
#for(I,0,Count-1)
 #declare A=(Case>=0?mod(Case,8):(Artifact>=0?Artifact:(Sheet?int(I/5):I)));
 #declare T=(Sheet?mod(I,5):Treatment);
 #declare PX=(mod(I,Cols)-(Cols-1)/2)*SX;
 #declare PY=((Rows-1)/2-int(I/Cols))*SY;
 #declare SourceMode=(T<2?0:1);
 #declare CurrentVolume=(T>0);
 #declare CurrentBounce=((T=3 & (!Sheet | SheetBounce)) | WaxBounce);
 #declare CurrentNormal=NormalStrength*(T=4?2.5:0.25);
 #if (Case>=0)
  #declare C=int(Case/8);
  #declare SourceMode=mod(C,3);
  #declare CurrentVolume=(mod(int(C/9),3)>0);
  #declare CurrentBounce=mod(int(C/27),2);
  #declare CurrentNormal=(int(C/54)>0?0.3:0.03);
 #end
 #if (SourceModeOverride>=0) #declare SourceMode=SourceModeOverride; #end
 light_group {
  light_source {<-0.4,0.6,-0.5> rgb <0.75,0.84,1>*0.5*Exposure*Fill area_light x*0.15,z*0.15,3,3 adaptive 0}
  light_source {<0.3,0.35,0.15> rgb <1,0.55,0.28>*0.45*Exposure*Fill}
  Specimen(A,T)
  box {<-0.052,-0.055,0.025>,<0.052,0.07,0.03> pigment {rgb <0.03,0.045,0.06>}}
  box {<-0.008,-0.043,-0.0005>,<0.008,0.041,0.0005> rotate y*35 translate <0.047,0,-0.01> pigment {rgb 0.8}}
  global_lights off
  translate <PX,PY,0>
 }
 text {ttf "timrom.ttf" Names[A] 0.0001,0 scale 0.004 translate <PX-0.04,PY-0.064,-0.01> pigment {rgb <0.6,0.72,0.8>} finish {emission 0.6*Exposure}}
#end
