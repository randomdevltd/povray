#version 3.7;
#ifndef (Source) #declare Source=0; #end
#ifndef (Surface) #declare Surface=0; #end
#ifndef (Draws) #declare Draws=64; #end
#ifndef (Method) #declare Method=4; #end
#ifndef (Zero) #declare Zero=0; #end
#ifndef (HG) #declare HG=0; #end
#ifndef (Eccentricity) #declare Eccentricity=0.5; #end
#ifndef (SceneVersion) #declare SceneVersion=3.7; #end
#version SceneVersion;
#ifndef (Reverse) #declare Reverse=0; #end
#ifndef (Modifier) #declare Modifier=0; #end
#ifndef (Absorb) #declare Absorb=0; #end
#ifndef (Density) #declare Density=1; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
#if (Surface)
 camera { orthographic location <0,10,0> look_at 0 right x*0.001 up z*0.001 }
 plane { y,0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
 #declare SourcePoint=<0,100,0>;
#else
 camera { orthographic location <0,0,-1> direction z right x*0.001 up y*0.001 }
 #declare SourcePoint=<0,0,100>;
#end
#if (Source=0)
 light_source { <0,0,100*(1-2*Reverse)> rgb 1 parallel point_at 0 }
#else
 #if (Source=1)
  light_source { SourcePoint rgb 2500.25 fade_distance 1 fade_power 2 }
 #else
  sphere { SourcePoint,0.01 pigment { rgbt 1 } hollow no_image
   interior { light { brightness 50000000 samples Draws } }
  }
 #end
#end
#if (!Surface)
box { <-1,-1,0>,<1,1,1> pigment { rgbt 1 } hollow
 interior {
  media { method Method #if (Method=4) resolution 0.02 #end
   scattering { 1, rgb <0.2,0.1,0> extinction 0 }
   #if (Absorb & !Zero) absorption rgb <0.1,0.2,0.3> #end
   density { rgb Density }
  }
  media { method Method #if (Method=4) resolution 0.02 #end
   scattering { #if (HG) 5 #else 4 #end, rgb <0.1,0.3,0.4> extinction 0 #if (HG) eccentricity Eccentricity #end }
  }
  #if (Zero)
   media { method Method #if (Method=4) resolution 0.02 #end scattering { 3, rgb 0 } #if (Absorb) absorption rgb <0.1,0.2,0.3> #end }
  #end
 }
}
#if (Modifier)
box { <-0.9,-0.9,0.25>,<0.9,0.9,0.75> pigment { rgbt 1 } hollow
 interior { media { method 4 resolution 0.02
  #if (Modifier=1)
   mix multiply density { rgb <0.5,0.25,0.75> }
  #else
   mix subtract scattering { 3, rgb <0.05,0.1,0.1> extinction 0 }
  #end
 } }
}
#end
#end
