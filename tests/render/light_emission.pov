#version 3.7;
#ifndef (SceneVersion) #declare SceneVersion=3.7; #end
#version SceneVersion;
#ifndef (Alias) #declare Alias=1; #end
#ifndef (Method) #declare Method=4; #end
#ifndef (Lumens) #declare Lumens=0; #end
#ifndef (Zero) #declare Zero=0; #end
#ifndef (Conflict) #declare Conflict=0; #end
#ifndef (Overlap) #declare Overlap=0; #end
#ifndef (OwnAbsorb) #declare OwnAbsorb=0; #end
#ifndef (Omit) #declare Omit=0; #end
#ifndef (Strength) #declare Strength=1; #end
#ifndef (White) #declare White=0; #end
#ifndef (Empty) #declare Empty=0; #end
#ifndef (Constant) #declare Constant=0; #end
#declare D = density { function { 0.25+0.25*x } color_map { [0 rgb 0] [1 rgb 1] } translate x*0.1 };
#declare E = <5,2,1>*Strength*(1-Zero);
global_settings { assumed_gamma 1 }
camera { orthographic location <0,5,-1> look_at 0 right x*2 up z*2 }
plane { y,0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
box { -1,1 pigment { rgbt 1 } hollow no_image
  interior {
    #if (!Empty)
      media { method Method mix add emission rgb 100 density { D } }
      media { method Method mix replace emission rgb <10,4,2>*Strength*(1-Zero)
        absorption rgb OwnAbsorb
        #if (!Constant) density { D } #end
      }
      media { method Method mix add emission rgb <2,1,0.5>*Strength*(1-Zero)
        #if (!Constant) density { D } #end
      }
      media { method Method mix multiply density { rgb 0.5 } }
      media { method Method mix subtract emission rgb <1,0.5,0.25>*Strength*(1-Zero)
        #if (!Constant) density { D } #end
      }
    #end
    light {
      #if (!Omit)
        #if (Alias) emission
        #else
          #if (Constant) pigment { rgb E*(1-Empty)*(1-White)+White }
          #else pigment { function { 0.25+0.25*x } color_map { [0 rgb 0] [1 rgb E*(1-Empty)] } translate x*0.1 }
          #end
        #end
      #end
      #if (Conflict=1) pigment { rgb 1 } #end
      #if (Conflict=2) emission #end
      #if (Conflict=3) emission #end
      brightness 0.08 #if (Lumens) lumens #else nits #end
      samples 16
    }
  }
  scale <0.35,0.25,0.3> rotate y*15 translate y*1.5
}
#if (Overlap)
box { <-1,0.6,-1>,<1,1.9,1> pigment { rgbt 1 } hollow no_image
  interior { media { method 4 absorption rgb 0.8 } }
}
#end
