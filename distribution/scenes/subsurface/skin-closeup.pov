#version 4.0;
#ifndef (Kind) #declare Kind=2; #end
#ifndef (Strength) #declare Strength=0.35; #end
#ifndef (Highlights) #declare Highlights=1; #end
#ifndef (Flat) #declare Flat=0; #end
#ifndef (Spacing) #declare Spacing=0.45; #end
#ifndef (Lighting) #declare Lighting=0; #end
global_settings {assumed_gamma 1 mm_per_unit 1000 max_trace_level 12 subsurface {method 2 samples 24,8 spacing Spacing}}
background {rgb <0.025,0.035,0.052>}
camera {orthographic location <0,0.003,-0.15> look_at <0,0.003,0> right x*0.041 up y*0.041*image_height/image_width}
#if (Lighting=0)
 light_source {<-0.08,0.04,-0.04> rgb <1.5,1.35,1.25> area_light x*0.02,y*0.02,4,4 adaptive 1}
 light_source {<0.04,0.015,-0.04> rgb <0.15,0.19,0.26>}
#else
 light_source {<0,0.02,0.04> rgb <2.4,2.2,2> area_light x*0.07,y*0.02,6,3 adaptive 1}
#end
#macro Skin(Kind)
  texture {
    pigment { rgb <0.78, 0.47, 0.34> }
    #if (!Flat)
      #if (Kind = 1)
        normal { granite Strength scale 0.055 }
      #elseif (Kind = 2)
        normal { wrinkles Strength scale <0.12, 0.55, 0.12> rotate z*12 }
      #end
    #end
    finish {
      ambient 0 diffuse 0.75 specular (Highlights*0.12) roughness 0.045
      subsurface { translucency <1.4, 0.75, 0.42> }
    }
  }
  interior { ior 1.4 }
#end

#macro Ear(Kind)
  union {
    difference {
      sphere { 0, 1 scale <1.04, 1.65, 0.32> }
      sphere { <0.04, 0.17, -0.29>, 1 scale <0.68, 1.13, 0.22> }
    }
    sphere { <0, 0.03, 0.055>, 1 scale <0.65, 1.12, 0.09> }
    sphere { <-0.12, -1.33, -0.035>, 1 scale <0.56, 0.51, 0.34> }
    sphere { <-0.63, -0.37, -0.19>, 1 scale <0.27, 0.38, 0.19> }
    difference {
      torus { 0.54, 0.10 rotate x*90 scale <0.95, 1.65, 1> translate <0.08, 0.17, -0.18> }
      box { <-2, -2, -2>, <-0.25, 2, 2> }
    }
    Skin(Kind)
  }
#end

object {Ear(Kind) rotate y*-12 scale 0.01}
