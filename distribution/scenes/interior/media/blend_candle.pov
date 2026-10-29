#version 4.0;
#ifndef (RenderView) #declare RenderView = 0; #end
#ifndef (Smoke) #declare Smoke = 1; #end
#ifndef (Shimmer) #declare Shimmer = 1; #end
#ifndef (FlameLight) #declare FlameLight = 1; #end
#ifndef (FlameSamples) #declare FlameSamples = 64; #end
#ifndef (WaterGlass) #declare WaterGlass = 1; #end
#ifndef (WaxSSLT) #declare WaxSSLT = 1; #end
#ifndef (WaxBounce) #declare WaxBounce = 0; #end
#ifndef (WaxBouncePasses) #declare WaxBouncePasses = 1; #end
#ifndef (Radio) #declare Radio = 0; #end
#ifndef (SmokeBoost) #declare SmokeBoost = 8; #end
#ifndef (ArtisticHeatBoost) #declare ArtisticHeatBoost = 2; #end
#ifndef (SootyWick) #declare SootyWick = 1; #end
#ifndef (WaxTransportScale) #declare WaxTransportScale = 1; #end
#ifndef (CandleLumens) #declare CandleLumens = 12; #end
#ifndef (VisibleEmissionScale) #declare VisibleEmissionScale = 0.0001; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 4; #end
#ifndef (Caustics) #declare Caustics = 1; #end
#ifndef (FlameTime) #declare FlameTime = 0.137+clock; #end
#declare Wick = <0,1.145,0>;
#declare WickY = Wick.y;
#declare Back = 1.6;
#include "functions.inc"
global_settings { assumed_gamma 1
 #if (Caustics & RenderView!=2) photons { method 2 quality PhotonQuality } #end prepared_media_cells 4000000 atmospheric_ior 1.000277 max_trace_level 24
 #if (WaxSSLT & RenderView!=2) mm_per_unit 1000 subsurface { method 2 samples 12,2 spacing 0.35 error_bound 0.1 memory 128 } #end
 #if (Radio & RenderView!=2) radiosity { count 180 error_bound 0.45 nearest_count 6 pretrace_start 0.32 pretrace_end 0.03 media on } #end
}
#if (RenderView=0)
 camera { location <2.75,2.0,-3.6> right x*image_width/image_height angle 73 look_at <-0.7,2.05,0> }
#elseif (RenderView=3)
 camera { location <0.43,1.32,-0.77> right x*image_width/image_height angle 50 look_at <0.10,1.10,0> }
#elseif (RenderView=4)
 camera { location <0.055,1.185,-0.15> right x*image_width/image_height angle 25 look_at <0.002,1.17,0> }
#elseif (RenderView=2)
 camera { orthographic location Wick+<0,0.0285,-0.1> direction z right x*0.050 up y*0.070 }
#else
 camera { location <0.43,1.32,-0.77> right x*image_width/image_height angle 33 look_at <0.035,1.15,0> }
#end
#if (RenderView!=2)
difference {
  box { <-3.1, -0.1, -4.1>, <3.1, 4.1, Back> }
  box { <-3, 0, -4>, <3, 4, Back + 1> }
  box { <-3.2, 1.6, -0.6>, <-2.9, 3.4, 0.8> }
  pigment { rgb <0.62, 0.55, 0.45> }
}
box {
  <-3.1, -0.1, Back>, <3.1, 4.1, Back + 0.1>
  pigment {
    brick pigment { rgb <0.9, 0.86, 0.78> } pigment { granite scale 0.3 color_map { [0 rgb <0.5, 0.15, 0.06>] [1 rgb <0.26, 0.07, 0.03>] } }
    brick_size <0.3, 0.1, 0.15> mortar 0.025
  }
}
box {
  <-3, 3.99, -4>, <3, 4.05, Back>
  pigment { gradient x color_map { [0 rgb 0.1] [0.07 rgb 0.1] [0.07 rgb <0.5, 0.33, 0.18>] [1 rgb <0.42, 0.27, 0.14>] } scale 0.3 }
}
light_source { <-6, 3.4, -3> rgb <0.8, 1, 1.6> spotlight point_at <-1.8, 1.4, Back> radius 9 falloff 13 photons { reflection off refraction off } }
box { <-0.9, 0, -0.6>, <0.9, 0.9, 0.6> pigment { rgb <0.3, 0.17, 0.09> } photons { collect on } }
#if (WaterGlass)
  difference {
    cylinder { <0.20,0.901,0.015>, <0.20,1.021,0.015>,0.035 }
    cylinder { <0.20,0.904,0.015>, <0.20,1.022,0.015>,0.033 }
    pigment { rgbt 1 } finish { diffuse 0 reflection { 0, 1 fresnel } conserve_energy }
    interior { ior 1.5 } photons { target refraction on reflection on collect off }
  }
  cylinder {
    <0.20,0.904,0.015>, <0.20,0.995,0.015>,0.0329
    pigment { rgbt 1 } finish { diffuse 0 reflection { 0, 1 fresnel } conserve_energy }
    interior { ior 1.33 } photons { target refraction on reflection on collect off }
  }
#end
union {
  box { <-0.52, -0.52, 0.01>, <0.52, 0.52, 0.04> pigment { rgb <0.20, 0.07, 0.04> } }
  box {
    <-0.45, -0.45, 0>, <0.45, 0.45, 0.01>
    pigment {
      function { 0.25 + 0.5 * abs(mod(floor(atan2(x, y) * 8 / pi + 8), 2) - mod(floor(sqrt(x * x + y * y) / 0.07), 2)) }
      color_map { [0.5 rgb <0.06, 0.1, 0.28>] [0.5 rgb <0.92, 0.85, 0.66>] }
    }
  }
  translate <-0.35, 2.05, Back - 0.04>
}
union {
 box { <-3.03,1.56,-0.64>, <-2.93,1.64,0.84> }
 box { <-3.03,3.36,-0.64>, <-2.93,3.44,0.84> }
 box { <-3.03,1.6,-0.64>, <-2.93,3.4,-0.56> }
 box { <-3.03,1.6,0.76>, <-2.93,3.4,0.84> }
 box { <-3.03,1.6,0.065>, <-2.93,3.4,0.135> }
 box { <-3.03,2.46,-0.6>, <-2.93,2.53,0.8> }
 pigment { rgb <0.26,0.16,0.08> } finish { diffuse 0.65 reflection 0.08 }
}
cylinder { <0,0.98,0>, <0,1.14,0>,0.0105 pigment { rgb <0.95,0.9,0.8> }
 light { emission brightness 1 nits samples 64 #if (WaxBounce) bounce { passes WaxBouncePasses } #end }
 #if (WaxSSLT) finish { diffuse 0.7 subsurface { method 2 translucency (<8,5,3>*WaxTransportScale) colour rgb <1,0.93,0.82> } } interior { ior 1.44 } #end
}
cylinder { <0,1.137,0>, Wick+<0.0003,0.001,0>,0.0005 pigment { rgb 0.025 } }
#declare OldBrass = texture { pigment { bozo turbulence 0.35 color_map { [0 rgb <0.08,0.055,0.025>] [0.35 rgb <0.28,0.19,0.065>] [0.65 rgb <0.6,0.42,0.15>] [1 rgb <0.35,0.27,0.12>] } scale 0.007 } normal { bumps 0.025 scale 0.002 } finish { diffuse 0.3 specular 0.65 roughness 0.045 metallic reflection 0.25 } }
union {
 lathe { linear_spline 13 <0,0>,<0.045,0>,<0.05,0.004>,<0.043,0.009>,<0.012,0.014>,<0.009,0.06>,<0.025,0.07>,<0.033,0.077>,<0.03,0.083>,<0.025,0.079>,<0.008,0.074>,<0.006,0.012>,<0,0.012> scale <2.4,1,2.4> }
 torus { 0.072,0.005 translate y*0.079 }
 torus { 0.035,0.006 rotate x*90 translate <-0.12,0.04,0> }
 cylinder { <-0.095,0.06,0>, <-0.014,0.06,0>,0.009 }
 cylinder { <-0.095,0.02,0>, <-0.014,0.02,0>,0.009 }
 texture { OldBrass } photons { collect on } translate y*0.9
}
#declare FlameHeight = 0.049;
#declare FlameCX = function(Y) { 0.002*sin(Y*93-FlameTime*18)*Y/FlameHeight + 0.008*pow(Y/FlameHeight,2)*sin(FlameTime*19-1.0322) };
#declare FlameCZ = function(Y) { 0.003*sin(Y*180+FlameTime*11)*Y/FlameHeight };
#declare CoreY = function(Y) { min(1,max(0,Y/FlameHeight)) };
#declare FlameRadius = function(Y) { 0.0012+0.0059*pow(max(0,sin(pi*CoreY(Y))),0.8) };
#declare TwistedX = function(X,Y,Z) { (X-FlameCX(Y))*cos(Y*90)+(Z-FlameCZ(Y))*sin(Y*90) };
#declare TwistedZ = function(X,Y,Z) { -(X-FlameCX(Y))*sin(Y*90)+(Z-FlameCZ(Y))*cos(Y*90) };
#declare Ragged = function(X,Y,Z) { 0.87+0.13*sin(X*1900+sin(Y*950-FlameTime*19))*sin(Z*1500+Y*430+FlameTime*13) };
#declare FlameR = function(X,Y,Z) { sqrt(pow(TwistedX(X,Y,Z)/(FlameRadius(Y)*(1+0.35*sin(Y*170+FlameTime*14))),2)+pow(TwistedZ(X,Y,Z)/(FlameRadius(Y)*(0.72+0.16*cos(Y*135-FlameTime*9))),2)) };
#declare MainLobe = function(X,Y,Z) { min(1,max(0,1-pow(FlameR(X,Y,Z),2)))*min(1,max(0,Y/0.002))*min(1,max(0,(FlameHeight-Y)/0.003))*Ragged(X,Y,Z)*(1-0.97*exp(-12*pow(FlameR(X,Y,Z),2))*max(0,1-Y/0.015)) };
#declare SideLobe = function(X,Y,Z) { min(1,max(0,1-pow((X-(0.004+0.002*sin(FlameTime*17))-(Y-0.025)*0.4)/0.003,2)-pow((Y-0.028)/0.011,2)-pow((Z+0.0015)/0.0028,2)))*Ragged(X,Y,Z)*0.78 };
#declare WispLobe = function(X,Y,Z) { min(1,max(0,1-pow((X-FlameCX(Y)-0.001*sin(Y*600))/0.0014,2)-pow((Y-0.047)/0.008,2)-pow((Z-FlameCZ(Y))/0.0016,2)))*0.6*Ragged(X,Y,Z) };
#declare FlameMask = function(X,Y,Z) { 1-(1-MainLobe(X,Y,Z))*(1-SideLobe(X,Y,Z))*(1-WispLobe(X,Y,Z)) };
#macro FlameBand(C)
 function { FlameMask(x,y,z) } color_map { [0 rgb 0] [1 rgb C] }
#end
#macro FlameColour()
 function { min(0.999999,max(0,y/0.055)) } density_map { [0 FlameBand(<0.04,0.12,1>)] [0.14 FlameBand(<0.14,0.25,1>)] [0.26 FlameBand(<1,0.22,0.025>)] [0.52 FlameBand(<1,0.68,0.15>)] [0.8 FlameBand(<1,0.38,0.035>)] [1 FlameBand(<0.8,0.07,0.008>)] }
#end
#declare EmissionAmplitude = 941729.020334*VisibleEmissionScale;
box { <-0.018,0,-0.014>, <0.020,0.057,0.014> pigment { rgbt 1 }
 interior { media { mix add method 4 resolution 0.00045 emission EmissionAmplitude density { FlameColour() } }
 #if (FlameLight & RenderView!=2) light { emission brightness CandleLumens lumens samples FlameSamples photons { reflection on refraction on } } #end
 } translate Wick
}
#declare AmbientT = 293.15;
#declare PlumeZ = function(Y) { max(0.1,Y) };
#declare DeltaTc = function(Y) { 3.975263/pow(PlumeZ(Y)-0.022575,5/3) };
#declare PlumeB = function(Y) { 0.12*sqrt((AmbientT+DeltaTc(Y))/AmbientT)*(PlumeZ(Y)-0.022575) };
#declare PlumeX = function(Y) { 0.007*sin(Y*4-clock)*Y/(0.2+Y) };
#declare HeatMean = function(X,Y,Z) { ArtisticHeatBoost*DeltaTc(Y)*exp(-ln(2)*(pow(X-PlumeX(Y),2)+Z*Z)/pow(PlumeB(Y),2))*min(1,max(0,Y/0.042))*pow(min(1,max(0,(2.845-Y)/0.25)),2) };
#declare HeatSmooth = function(T) { pow(min(1,max(0,T)),2)*(3-2*min(1,max(0,T))) };
#declare HeatY = function(Y) { HeatSmooth((0.4-Y)/0.2) };
#declare HeatWindow = function(X,Y,Z) { HeatY(Y)*HeatSmooth((0.15-sqrt(X*X+Z*Z))/0.05) };
#declare HeatNMean = function(X,Y,Z) { 0.000277*(AmbientT/(AmbientT+HeatMean(X,Y,Z))-1) };
#declare HeatNFine = function(X,Y,Z) { 0.000277*(AmbientT/(AmbientT+HeatMean(X,Y,Z)*(1+0.08*sin(Y*180-clock*8)*sin(X*400+Z*310)))-1)*HeatWindow(X,Y,Z) };
#if (Shimmer)
box { <-1.5,0,-1.5>, <1.5,2.845,1.5> pigment { rgbt 1 } hollow
 interior { ior off refraction { function { HeatNMean(x,y,z)*(1-HeatY(y)) } detail 0.04 } }
 translate Wick
}
box { <-0.15,0,-0.15>, <0.15,0.4,0.15> pigment { rgbt 1 } hollow
 interior { ior off refraction { function { HeatNFine(x,y,z) } detail 0.0045 } }
 translate Wick
}
#end
#declare SootRate = (0.1+3.9*SootyWick)*1e-6/3600;
#declare SmokeExtinction = 8700;
#declare SmokeSSA = 0.3;
#declare BurnSeconds = 1800;
#declare CeilingLayer = 0.35;
#declare SmokeU = function(Y) { 0.4105/pow(PlumeZ(Y)-0.022575,1/3) };
#declare SmokeC = function(X,Y,Z) { 2*SootRate/(pi*SmokeU(Y)*pow(PlumeB(Y),2))*exp(-(pow(X-PlumeX(Y),2)+Z*Z)/pow(PlumeB(Y),2))*min(1,max(0,(Y-0.025)/0.017))*pow(min(1,max(0,(2.845-Y)/0.35)),2) };
#declare LayerC = 3*SootRate*BurnSeconds/(5.98*5.58*CeilingLayer);
#declare SmokeField = function(X,Y,Z) { (SmokeC(X,Y,Z)+LayerC*pow(min(1,max(0,(Y-(2.845-CeilingLayer))/CeilingLayer)),2)) };
#if (Smoke)
box { <-2.99,0,-3.99>, <2.99,2.845,1.59> pigment { rgbt 1 } hollow
 interior { media { mix add method 4 resolution 0.04 scattering { 1,rgb (SmokeExtinction*SmokeBoost*SmokeSSA) } absorption SmokeExtinction*SmokeBoost*(1-SmokeSSA) density { function { SmokeField(x,y,z) } } } }
 translate Wick
}
#end
box { <-2.99,0.01,-3.99>, <2.99,3.98,Back-0.01> pigment { rgbt 1 } hollow
 interior { media { mix add method 4 resolution 0.16 scattering { 1,rgb 0.012 } absorption 0.003 } }
}
#end
