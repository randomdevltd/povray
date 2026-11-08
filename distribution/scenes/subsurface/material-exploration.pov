#version 4.0;
#include "functions.inc"
#ifndef (LibraryOnly) #declare LibraryOnly=0; #end
#ifndef (Coating) #declare Coating=1; #end
#ifndef (Detail) #declare Detail=1; #end
#ifndef (AreaSamples) #declare AreaSamples=9; #end
#ifndef (Treatment) #declare Treatment=2; #end
#ifndef (Artifact) #declare Artifact=-1; #end
#ifndef (Case) #declare Case=-1; #end
#ifndef (Sheet) #declare Sheet=0; #end
#ifndef (SubsurfaceSamples) #declare SubsurfaceSamples=64; #end
#ifndef (InternalSamples) #declare InternalSamples=128; #end
#ifndef (EmitterSamples) #declare EmitterSamples=96; #end
#ifndef (VeinPattern) #declare VeinPattern=1; #end
#ifndef (VeinScale) #declare VeinScale=0.005; #end
#ifndef (VeinLayer) #declare VeinLayer=1; #end
#ifndef (VeinInset) #declare VeinInset=0.0007; #end
#ifndef (VeinThickness) #declare VeinThickness=0.0008; #end
#ifndef (HandDomainCells) #declare HandDomainCells=2000000; #end
#ifndef (CameraZoom) #declare CameraZoom=1; #end
#ifndef (PanelHalfDepth) #declare PanelHalfDepth=0.006; #end
#ifndef (PanelRound) #declare PanelRound=1; #end
#ifndef (EmissionExport) #declare EmissionExport=0; #end
#ifndef (Export) #declare Export=0; #end
#ifndef (SurfaceSource) #declare SurfaceSource=0; #end
#ifndef (SheetBounce) #declare SheetBounce=0; #end
#ifndef (WaxLength) #declare WaxLength=0.083; #end
#ifndef (WaxRadius) #declare WaxRadius=0.0105; #end
#ifndef (CandleLumens) #declare CandleLumens=0.025; #end
#ifndef (LampLumens) #declare LampLumens=0.015; #end
#ifndef (InternalLumens) #declare InternalLumens=0.00125; #end
#ifndef (HandLumens) #declare HandLumens=0.000625; #end
#ifndef (WaxBounce) #declare WaxBounce=0; #end
#ifndef (WaxBouncePasses) #declare WaxBouncePasses=1; #end
#ifndef (SourceOn) #declare SourceOn=1; #end
#ifndef (NormalStrength) #declare NormalStrength=0.18; #end
#ifndef (InternalSource) #declare InternalSource=(Case<0 & !Sheet); #end
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
#declare CircuitX=function(X) {mod(X+0.0285,0.019)-0.0095};
#declare SegmentT=function(X,Y,AX,AY,BX,BY) {min(1,max(0,((X-AX)*(BX-AX)+(Y-AY)*(BY-AY))/(pow(BX-AX,2)+pow(BY-AY,2))))};
#declare SegmentDistance=function(X,Y,AX,AY,BX,BY) {sqrt(pow(X-AX-(BX-AX)*SegmentT(X,Y,AX,AY,BX,BY),2)+pow(Y-AY-(BY-AY)*SegmentT(X,Y,AX,AY,BX,BY),2))};
#declare RuneMask=function(X,Y) {max(0,1-min(
 SegmentDistance(RuneX(X),RuneY(Y),-0.0045,-0.006,-0.0045,0.006),
 SegmentDistance(RuneX(X),RuneY(Y),-0.0045,0.006,0.0045,0.003),
 SegmentDistance(RuneX(X),RuneY(Y),-0.0045,0,0.003,0),
 SegmentDistance(RuneX(X),RuneY(Y),-0.0045,-0.006,0.0045,-0.003)
 )/0.00055)};
#declare CircuitMask=function(X,Y) {max(0,1-min(
 abs(CircuitX(X)),abs(Y-0.0056),
 SegmentDistance(X,Y,-0.019,0.018,-0.009,0.028),
 SegmentDistance(X,Y,0,0.018,0.010,0.028),
 SegmentDistance(X,Y,0.019,-0.009,0.009,-0.019),
 abs(sqrt(pow(X+0.019,2)+pow(Y-0.030,2))-0.0022),
 abs(sqrt(pow(X,2)+pow(Y-0.030,2))-0.0022),
 abs(sqrt(pow(X-0.019,2)+pow(Y-0.030,2))-0.0022)
 )/0.0006)};
#macro CapillaryPattern()
 crackle form <-1,1,0> metric 2 turbulence 0.6 octaves 5 omega 0.58 lambda 2.2
 scale <VeinScale,VeinScale*3.6,1000>
#end
#declare CapillaryDensity=density {
 CapillaryPattern()
 colour_map {[0 rgb 1] [0.02 rgb 1] [0.06 rgb 0] [1 rgb 0]}
};
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
 subsurface {method 2 samples SubsurfaceSamples,InternalSamples spacing 1 error_bound 0.15} }
#default {finish {ambient 0 diffuse 0.7}}
#if (!LibraryOnly)
background {rgb <0.012,0.019,0.031>}
#declare Single=(Artifact>=0 | Case>=0);
#declare Cols=(Sheet?5:(Single?1:4));
#declare Rows=(Sheet?8:(Single?1:2));
#declare SX=0.12;
#declare SY=(Artifact=2?max(0.17,WaxLength+0.085):0.17);
#declare ViewWidth=max(Cols*SX+0.035,(Rows*SY+0.025)*image_width/image_height);
camera {orthographic location <0,0.07,-0.8> look_at <0,0,0> right x*ViewWidth/CameraZoom up y*ViewWidth*image_height/image_width/CameraZoom}
plane {z,0.07 pigment {rgb <0.026,0.038,0.056>} finish {diffuse 0.7}}
#end
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
  normal {
   #switch(A)
   #case(2) wrinkles CurrentNormal*0.3 scale <0.004,0.018,0.004> #break
   #case(3) wrinkles CurrentNormal*0.3 scale <0.002,0.012,0.002> #break
   #case(4) granite CurrentNormal*0.65 scale 0.00065 #break
   #case(5) wrinkles CurrentNormal*0.4 scale <0.0005,0.004,0.0005> #break
   #case(6) bumps CurrentNormal*0.25 scale 0.00015 #break
   #case(7) wrinkles CurrentNormal*0.6 scale <0.00015,0.003,0.00015> #break
   #else bumps CurrentNormal scale 0.00035
   #end
  }
  finish {diffuse 0.7 specular 0.3 roughness 0.045
   subsurface {method 2 translucency Dist*K colour rgb 0.95 thickness 0.4
    #if (A=0 & SourceMode=1 & !InternalSource) emission pigment {Veins} #end
    #if (A=6 & SourceMode=1 & !InternalSource) emission pigment {object {Circuit rgb 0 rgb <0.05,1,1.7>*Exposure*3}} #end
    #if (A=7 & SourceMode=1 & !InternalSource) emission pigment {object {Runes rgb 0 rgb <1.8,0.32,0.04>*Exposure*3}} #end
    #if (CurrentVolume & !InternalSource) volume_sampling {depth D spread Spread} #end
   }
  }
 }
 #if(Coating)
  texture {
   pigment {
    crackle form <-1,1,0> metric 2 turbulence 0.35
    scale <0.009,0.018,1000>
    colour_map {[0 rgb <0.10,0.055,0.13>] [0.012 rgb <0.10,0.055,0.13>] [0.025 rgbt <1,1,1,1>] [1 rgbt <1,1,1,1>]}
   }
   normal {bumps 0.08 scale 0.00018}
   finish {ambient 0 diffuse 0.65 specular 0.25 roughness 0.035}
  }
 #end
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
  cone {<0,-0.026,0>,0.015-Inset,<0,-0.05,0.001>,0.011-Inset}
  #for(J,0,3)
   #local L=0.044+0.009*sin(J*pi/3);
   #local P=<(J-1.5)*0.012,0.008,0>;
   #local Q=<(J-1.5)*0.014,L*0.65,-0.001>;
   #local R=<(J-1.5)*0.015,L,0>;
   sphere {P,0.0057-Inset}
   cone {P,0.0057-Inset,Q,0.0049-Inset}
   sphere {Q,0.0049-Inset}
   cone {Q,0.0049-Inset,R,0.0038-Inset}
   sphere {R,0.0038-Inset}
  #end
  sphere {<-0.019,-0.012,0>,0.0075-Inset}
  cone {<-0.019,-0.012,0>,0.0075-Inset,<-0.031,0.001,-0.001>,0.006-Inset}
  sphere {<-0.031,0.001,-0.001>,0.006-Inset}
  cone {<-0.031,0.001,-0.001>,0.006-Inset,<-0.038,0.009,0>,0.0045-Inset}
  sphere {<-0.038,0.009,0>,0.0045-Inset}
 }
#end
#declare Hardware=texture {pigment {rgb <0.18,0.14,0.085>} normal {bumps 0.08 scale 0.0002} finish {diffuse 0.45 metallic specular 0.65 roughness 0.025}};
#macro Wire(P,Q,R,C)
 cylinder {P,Q,R pigment {rgb C} finish {specular 0.2 roughness 0.05}}
#end
#macro Specimen(A,T)
 #switch(A)
 #case(0)
  object {HandBoundary(0) Flesh(A,T)}
  #if(Detail)
   #for(J,0,3)
    #local L=0.044+0.009*sin(J*pi/3);
    sphere {0,1 scale <0.0028,0.0046,0.00045> translate <(J-1.5)*0.015,L-0.001,-0.0036> pigment {rgb <0.36,0.22,0.38>} finish {specular 0.45 roughness 0.025}}
    #for(H,0,1)
     torus {0.0048,0.00015 rotate x*90 scale <0.8,0.45,1> translate <(J-1.5)*0.014,L*(0.47+H*0.20),-0.0009> pigment {rgb <0.12,0.07,0.17>}}
    #end
   #end
  #end
  #break
 #case(1)
  merge {
   difference {
    sphere {0,1 scale <0.025,0.039,0.008>}
    sphere {0,1 scale <0.018,0.030,0.007> translate <0.002,0.004,-0.006>}
   }
   sphere {0,1 scale <0.016,0.028,0.003> translate <0,0.001,0.003>}
   sphere {0,1 scale <0.012,0.010,0.008> translate <0,-0.032,0>}
   sphere {0,1 scale <0.005,0.008,0.004> translate <-0.013,-0.009,-0.004>}
   difference {
    torus {0.012,0.0022 rotate x*90 scale <1,1.65,1> translate <0.003,0.005,-0.003>}
    box {<-0.04,-0.04,-0.02>,<-0.003,0.04,0.02>}
   }
   sphere {<0.011,-0.009,-0.004>,0.002}
   cone {<0.011,-0.009,-0.004>,0.002,<0.005,0.010,-0.003>,0.002}
   sphere {<0.005,0.010,-0.003>,0.002}
   cone {<0.005,0.010,-0.003>,0.002,<-0.002,0.020,-0.003>,0.0017}
   sphere {<-0.002,0.020,-0.003>,0.0017}
   Flesh(A,T)
  } #break
 #case(2)
  difference {cylinder {<0,0.040-WaxLength,0>,<0,0.040,0>,WaxRadius} sphere {<0,0.047,0>,0.014} Flesh(A,T)}
  #if(Detail)
   #for(J,0,8)
    #local Theta=J*137.5; #local Length=0.007+0.019*abs(sin(J*7));
    merge {
     #local P=<WaxRadius*sin(Theta*pi/180),0.037,WaxRadius*cos(Theta*pi/180)>;
     #local Q=P-y*Length;
     sphere {P,0.0018}
     cone {P,0.0018,Q,0.0014}
     sphere {Q,0.0014}
     Flesh(A,T)
    }
   #end
   torus {WaxRadius+0.004,0.0012 translate y*(0.038-WaxLength) texture {Hardware}}
  #end
  cylinder {<0,0.034,0>,<0,0.047,0>,0.0007 pigment {rgb 0.025}}
  #local FlameAxes=<0.004,0.013,0.004>;
  #local FlameColour=colour rgb <3,1.07143,0.17143>;
  #local FlameVolume=4*pi*FlameAxes.x*FlameAxes.y*FlameAxes.z/3;
  #local FlameEmission=FlameColour*CandleLumens/(pi*FlameVolume*FlameColour.gray);
  sphere {0,1 pigment {rgbt 1}
   interior {media {mix add method 4 domain_cells 1664 emission FlameEmission*Exposure*SourceOn density {spherical}}
    #if(SourceOn) light {emission brightness (CandleLumens*Exposure) lumens samples EmitterSamples} #end
   }
   scale FlameAxes translate y*0.058
  }
  cylinder {<0,0.036-WaxLength,0>,<0,0.040-WaxLength,0>,WaxRadius+0.008 pigment {rgb <0.25,0.17,0.065>} finish {specular 0.5 metallic}}
  #break
 #case(3)
  difference {sphere {0,1 scale <0.033,0.046,0.020>} sphere {0,1 scale <0.031,0.044,0.018>} box {<-0.05,-0.06,-0.04>,<0.05,-0.035,0.04>} Flesh(A,T)}
  #if(Detail)
   #for(J,0,11)
    intersection {
     torus {0.037,0.00045 rotate x*90 scale <0.89,1.23,0.54> rotate y*(J*30)}
     box {<-0.04,-0.034,-0.04>,<0.04,0.05,0.04>}
     texture {Hardware}
    }
   #end
   cylinder {<0,-0.045,0>,<0,-0.034,0>,0.006 texture {Hardware}}
   cylinder {<0,-0.048,0>,<0,-0.045,0>,0.020 texture {Hardware}}
   torus {0.017,0.0015 translate y*-0.046 texture {Hardware}}
  #end
  sphere {0,0.004 pigment {rgbt 1}
   interior {ior off media {mix add method 4 emission <1,0.45,0.13>*Exposure*SourceOn}
    #if(SourceOn) light {emission brightness (LampLumens*Exposure) lumens samples EmitterSamples} #end
   }
  }
  #break
 #case(4)
  difference {sphere {0,1 scale <0.036,0.043,0.028>} box {<-0.05,-0.05,-0.05>,<0.05,0.05,-0.006>} Flesh(A,T)}
  sphere {0,1 scale <0.009,0.032,0.003> translate <0,0,-0.0065> pigment {rgb <0.95,0.83,0.48>}}
  #if(Detail)
   torus {0.034,0.0014 rotate x*90 scale <1,1.23,1> translate z*-0.006 pigment {rgb <0.33,0.19,0.025>} normal {bumps 0.3 scale 0.0004}}
   #for(J,0,9)
    #local Theta=J*pi/5;
    Wire(<0.008*sin(Theta),0.008*cos(Theta),-0.0068>,<0.030*sin(Theta),0.036*cos(Theta),-0.0068>,0.00025,<0.98,0.77,0.40>)
   #end
   sphere {0,1 scale <0.004,0.006,0.004> translate y*0.043 pigment {rgb <0.16,0.08,0.015>} normal {wrinkles 0.4 scale 0.001}}
  #end
  #for(J,-1,1,2) #for(H,-1,1,2) sphere {0,1 scale <0.002,0.004,0.001> translate <J*0.012,H*0.012,-0.007> pigment {rgb <0.15,0.045,0.012>}} #end #end
  #break
 #case(5)
  intersection {sphere {0,1 scale <0.044,0.054,0.002> translate x*0.02} sphere {0,1 scale <0.044,0.054,0.002> translate x*-0.02} Flesh(A,T)}
  cylinder {<0,-0.045,-0.002>,<0,0.045,-0.002>,0.0008 pigment {rgb <0.1,0.22,0.03>}}
  #for(J,-3,3) #for(S,-1,1,2)
   #local Tip=<S*0.018*(1-abs(J)/5),J*0.01+0.012,-0.002>;
   Wire(<0,J*0.01,-0.002>,Tip,0.0003,<0.12,0.27,0.035>)
   #if(Detail) #for(H,1,3)
    #local Start=Tip*H/4+<0,J*0.01,-0.002>*(1-H/4);
    Wire(Start,Start+<S*0.0035,0.006,0>,0.0001,<0.18,0.34,0.06>)
   #end #end
  #end #end
  Wire(<0,-0.043,0>,<0,-0.060,0.001>,0.00065,<0.16,0.25,0.04>)
  #break
 #case(6)
  #if (PanelRound)
   superellipsoid {<0.18,0.18> scale <0.037,0.045,PanelHalfDepth> Flesh(A,T)}
  #else
   box {<-0.037,-0.045,-PanelHalfDepth>,<0.037,0.045,PanelHalfDepth> Flesh(A,T)}
  #end
  #for(J,-1,1) cylinder {<J*0.019,-0.028,-PanelHalfDepth-0.0002>,<J*0.019,-0.028,-PanelHalfDepth-0.002>,0.005 pigment {rgb <0.08,0.11,0.15>} finish {specular 0.6}} #end
  #if(Detail)
   difference {
    superellipsoid {<0.15,0.15> scale <0.040,0.048,PanelHalfDepth+0.0008>}
    box {<-0.036,-0.044,-0.02>,<0.036,0.044,0.02>}
    texture {Hardware}
   }
   #for(J,-1,1,2) #for(H,-1,1,2)
    difference {
     cylinder {<J*0.036,H*0.044,-PanelHalfDepth-0.001>,<J*0.036,H*0.044,-PanelHalfDepth-0.0015>,0.0016}
     box {<-0.002,-0.00025,-0.001>,<0.002,0.00025,0.001> rotate z*30 translate <J*0.036,H*0.044,-PanelHalfDepth-0.0015>}
     texture {Hardware}
    }
   #end #end
   #for(J,0,4) Wire(<-0.026+J*0.003,-0.036,-PanelHalfDepth-0.0002>,<-0.024+J*0.003,-0.036,-PanelHalfDepth-0.0002>,0.00022,<0.75,0.67,0.40>) #end
  #end
  #break
 #case(7)
  box {<-0.034,-0.041,-0.001>,<0.034,0.041,0.001> Flesh(A,T)}
  #for(J,-1,1,2)
   cylinder {<-0.038,J*0.043,0>,<0.038,J*0.043,0>,0.004 Flesh(A,T)}
   #if(Detail)
    #for(S,-1,1,2)
     torus {0.0035,0.00045 rotate z*90 translate <S*0.039,J*0.043,0> texture {Hardware}}
     sphere {<S*0.041,J*0.043,0>,0.002 texture {Hardware}}
    #end
    #for(H,0,5) Wire(<-0.032+H*0.012,J*0.040,-0.0025>,<-0.027+H*0.012,J*0.040,-0.0025>,0.00015,<0.35,0.19,0.06>) #end
   #end
  #end
  #break
 #end
 #if (SourceOn & InternalSource & SourceMode=1 & (A=0 | A=6 | A=7))
  #local Glow=(A=7?<1.8,0.32,0.04>:<0.05,0.4,1.7>);
  #local Glow=Glow/max(Glow.x,Glow.y,Glow.z);
  // 0.25 mm or 1 mm cells: the vein layer's bounds are 0.0707 x 0.1055 x 0.0296, the flat layer and panels 0.0004 deep
  #local Cells=(A=0?(VeinLayer?(VeinPattern?14211694:225780):(VeinPattern?238852:7526)):(A=7?4608:3450));
  #if (A=0)
   #if (VeinLayer)
    difference {HandBoundary(VeinInset) HandBoundary(VeinInset+VeinThickness)
   #else
    intersection {HandBoundary(0.0007) box {<-0.06,-0.06,-0.0002>,<0.06,0.07,0.0002>}
   #end
  #else
   box {<-(A=7?0.032:0.025),-(A=7?0.036:0.034),-0.0002>,<(A=7?0.032:0.025),(A=7?0.036:0.035),0.0002>
  #end
  pigment {rgbt 1}
   interior {ior off media {mix add method 4 domain_cells Cells emission Glow*Exposure
    density {
     #if(A=0)
      #if(VeinPattern) CapillaryDensity
      #else function {VeinField(x,y,z).gray} colour_map {[0 rgb 0] [1 rgb 1]} #end
     #elseif(A=6) function {CircuitMask(x,y)} colour_map {[0 rgb 0] [1 rgb 1]}
     #else function {RuneMask(x,y)} colour_map {[0 rgb 0] [1 rgb 1]} #end
    }
   }
   light {emission brightness (A=0?HandLumens:InternalLumens)*Exposure lumens samples EmitterSamples #if(A=0 & VeinPattern) domain_cells HandDomainCells #end}
   }
  }
 #end

#end
#macro MatrixSpecimen(A,C)
 #declare SourceOn=(C>0);
 #declare SourceMode=SourceOn;
 #declare InternalSource=1;
 #declare Fill=(C=2?0:1);
 #declare Coating=(C>=3);
 #declare Export=(C=4);
 #declare CurrentBounce=0;
 #declare CurrentVolume=1;
 #declare CurrentNormal=NormalStrength;
 #declare TransportScale=(A=0?0.2:1);
 #declare EmissionExport=(C=4);
 #declare HandLumens=0.02;
 #declare InternalLumens=0.012;
 #declare VeinScale=0.008;
 #declare HandDomainCells=500000;
 light_source {<-0.4,0.6,-0.5> rgb <0.75,0.84,1>*0.5*Exposure*Fill area_light x*0.15,z*0.15,AreaSamples,AreaSamples adaptive 1}
 light_source {<0.3,0.35,0.15> rgb <1,0.55,0.28>*0.45*Exposure*Fill}
 Specimen(A,2)
 #if (SourceOn & (A=1 | A=4 | A=5))
  light_source {<0,0,0.045> rgb <1,0.55,0.22>*0.8}
 #end
#end
#if (!LibraryOnly)
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
 #declare CurrentNormal=NormalStrength*(T=4?1.8:1);
 #if (Case>=0)
  #declare C=int(Case/8);
  #declare SourceMode=mod(C,3);
  #declare CurrentVolume=(mod(int(C/9),3)>0);
  #declare CurrentBounce=mod(int(C/27),2);
  #declare CurrentNormal=(int(C/54)>0?0.3:0.03);
 #end
 #if (SourceModeOverride>=0) #declare SourceMode=SourceModeOverride; #end
 light_group {
  light_source {<-0.4,0.6,-0.5> rgb <0.75,0.84,1>*0.5*Exposure*Fill area_light x*0.15,z*0.15,AreaSamples,AreaSamples adaptive 1}
  light_source {<0.3,0.35,0.15> rgb <1,0.55,0.28>*0.45*Exposure*Fill}
  Specimen(A,T)
  box {<-0.052,-0.055,0.025>,<0.052,0.07,0.03> pigment {rgb <0.03,0.045,0.06>}}
  box {<-0.008,-0.043,-0.0005>,<0.008,0.041,0.0005> rotate y*35 translate <0.047,0,-0.01> pigment {rgb 0.8}}
  global_lights off
  translate <PX,PY,0>
 }
 #if (!Single | CameraZoom<=1.25)
 text {ttf "timrom.ttf" Names[A] 0.0001,0 scale 0.004 translate <PX-0.04,PY-0.064,-0.01> pigment {rgb <0.6,0.72,0.8>} finish {emission 0.6*Exposure}}
 #end
#end

#end
