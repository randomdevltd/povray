#version 4.0;
#declare LibraryOnly=1;
#declare Sheet=0;
#declare Artifact=-1;
#declare Case=-1;
#declare InternalSource=1;
#declare TransportScale=0.2;
#declare HandLumens=0.02;
#declare VeinScale=0.008;
#declare HandDomainCells=500000;
#declare Fill=1;
#include "material-exploration.pov"

#declare MatrixWidth=0.96;
#declare MatrixPitchX=0.145;
#declare MatrixPitchY=0.145;
#declare MatrixNames=array[8] {"ALIEN HAND","SKIN / EAR","WAX / FLAME","LAMP MEMBRANE","FRUIT / CORE","LEAF RIBS","ALIEN PANEL","RUNE SCROLL"};
#declare MatrixSources=array[8] {"embedded capillaries","rear point source","flame above wax","central lamp source","rear point source","rear point source","embedded circuits","embedded runes"};
#declare MatrixHeader=array[5] {"1  EXTERNAL ONLY","2  SOURCE + FILL","3  SOURCE ONLY","4  SPARSE COAT","5  SURFACE EXPORT"};
#declare MatrixSubhead=array[5] {"source OFF | fill 1","source ON | fill 1","source ON | fill 0","source ON | fill 1","source ON | fill 1"};
#declare MatrixCoats=array[5] {"coat OFF | export OFF","coat OFF | export OFF","coat OFF | export OFF","coat ON | export OFF","coat ON | export ON"};
#macro MatrixText(S,P,Size,Colour)
 text {ttf "cyrvetic.ttf" S 0.00001,0 no_shadow no_reflection scale Size translate P pigment {rgb Colour} finish {ambient 0 diffuse 0 emission 1}}
#end
background {rgb <0.018,0.025,0.038>}
camera {orthographic location <0,0,-1> look_at 0 right x*MatrixWidth up y*MatrixWidth*image_height/image_width}
plane {z,0.075 pigment {rgb <0.026,0.038,0.056>} finish {ambient 0 diffuse 0 emission 1}}
MatrixText("SUBSURFACE MATERIALS",<-0.455,0.698,-0.12>,0.019,<0.94,0.97,1>)
MatrixText("8 OBJECTS x 5 CONTROLLED VERSIONS",<-0.455,0.673,-0.12>,0.010,<0.65,0.78,0.90>)
MatrixText("Read across: same geometry, scattering, source power, camera and normal detail.",<-0.455,0.650,-0.12>,0.0093,<0.75,0.83,0.90>)
#for(C,0,4)
 #local X=-0.248+C*MatrixPitchX;
 MatrixText(MatrixHeader[C],<X-0.064,0.608,-0.12>,0.0093,<0.94,0.97,1>)
 MatrixText(MatrixSubhead[C],<X-0.064,0.590,-0.12>,0.0093,<0.74,0.84,0.94>)
 MatrixText(MatrixCoats[C],<X-0.064,0.573,-0.12>,0.0093,<0.74,0.84,0.94>)
#end
#for(A,0,7)
 #local Y=0.48-A*MatrixPitchY;
 MatrixText(MatrixNames[A],<-0.455,Y+0.028,-0.12>,0.010,<0.94,0.97,1>)
 MatrixText("SOURCE:",<-0.455,Y+0.005,-0.12>,0.0093,<0.56,0.72,0.86>)
 MatrixText(MatrixSources[A],<-0.455,Y-0.013,-0.12>,0.0093,<0.78,0.85,0.92>)
 #local KLabel="K = 1.00";
 #if(A=0) #local KLabel="K = 0.20"; #end
 MatrixText(KLabel,<-0.455,Y-0.038,-0.12>,0.0093,<0.78,0.85,0.92>)
 #for(C,0,4)
  #local X=-0.248+C*MatrixPitchX;
  light_group {
   MatrixSpecimen(A,C)
   box {<-0.058,-0.064,0.056>,<0.061,0.071,0.060> pigment {rgb <0.035,0.050,0.071>} finish {ambient 0 diffuse 0 emission 0.7}}
   box {<-0.008,-0.043,-0.0005>,<0.008,0.041,0.0005> rotate y*35 translate <0.047,0,-0.01> pigment {rgb 0.8} finish {ambient 0 diffuse 0.7}}
   global_lights off
   translate <X,Y,0>
  }
  MatrixText(concat("R",str(A+1,0,0)," / C",str(C+1,0,0)),<X-0.054,Y-0.074,-0.12>,0.0093,<0.56,0.72,0.86>)
 #end
#end
MatrixText("HOW TO READ THIS MATRIX",<-0.455,-0.657,-0.12>,0.011,<0.94,0.97,1>)
MatrixText("C1 > C2: add source.  C2 > C3: remove fill.  C2 > C4: add narrow sparse coat.",<-0.455,-0.679,-0.12>,0.0093,<0.78,0.85,0.92>)
MatrixText("C4 > C5: export subsurface + bounced light. Pale side strips are diffuse receivers.",<-0.455,-0.699,-0.12>,0.0093,<0.78,0.85,0.92>)
MatrixText("Homogeneous transport; patterns define emitters / surface coats, not varying bulk scattering.",<-0.455,-0.719,-0.12>,0.0093,<0.65,0.78,0.90>)
