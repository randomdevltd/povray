#version 3.7;
#ifndef (SceneVersion) #declare SceneVersion=3.7; #end
#version SceneVersion;
#ifndef (Alias) #declare Alias=1; #end
#ifndef (Method) #declare Method=4; #end
#ifndef (Lumens) #declare Lumens=0; #end
#ifndef (Nits) #declare Nits=0; #end
#ifndef (Hand) #declare Hand=1; #end
#ifndef (Flux) #declare Flux=0; #end
#ifndef (Bounce) #declare Bounce=0; #end
#ifndef (Zero) #declare Zero=0; #end
#ifndef (Conflict) #declare Conflict=0; #end
#ifndef (Overlap) #declare Overlap=0; #end
#ifndef (OwnAbsorb) #declare OwnAbsorb=0; #end
#ifndef (Omit) #declare Omit=0; #end
#ifndef (Strength) #declare Strength=1; #end
#ifndef (White) #declare White=0; #end
#ifndef (Empty) #declare Empty=0; #end
#ifndef (Constant) #declare Constant=0; #end
#ifndef (Samples) #declare Samples=16; #end
#declare D = density { function { 0.25+0.25*x } color_map { [0 rgb 0] [1 rgb 1] } translate x*0.1 };
#declare E = <5,2,1>*Strength*(1-Zero);
// a pigment's nits divide by the box's mean chord 4V/A; a unitless alias does not, a nits alias divides by its mean
#declare Chord = 2*0.7*0.5*0.6/(0.7*0.5+0.5*0.6+0.6*0.7);
#declare DF = function { pigment { function { 0.25+0.25*x } color_map { [0 rgb 0] [1 rgb 1] } translate x*0.1 } };
#declare MeanD = 0;
#for (I, 0, 1999) #declare MeanD = MeanD + DF(-1+(I+0.5)/1000, 0, 0).red/2000; #end
#declare Mean = (0.297*E.x + 0.589*E.y + 0.114*E.z)*(Constant ? 1 : MeanD);
// with Nits, Hand writes the normalized pigment by hand and unitless; without it the raw pigment takes nits
#if (Lumens | Flux | (Nits & !Hand)) #declare M = E; #elseif (Nits & Mean > 0) #declare M = E/Mean; #else #declare M = E*Chord; #end
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
          #if (Constant) pigment { rgb M*(1-Empty)*(1-White)+White }
          #else pigment { function { 0.25+0.25*x } color_map { [0 rgb 0] [1 rgb M*(1-Empty)] } translate x*0.1 }
          #end
        #end
      #end
      #if (Conflict=1) pigment { rgb 1 } #end
      #if (Conflict=2) emission #end
      #if (Conflict=3) emission #end
      // Flux states the 0.08 lumens as nits, the mean boundary radiance flux / (pi A) over the box's area 2.14
      brightness #if (Lumens) 0.08 lumens #elseif (Flux) 0.08/(pi*2.14) nits #else 0.2 #if (Nits & (Alias | !Hand)) nits #end #end
      samples Samples
      #if (Bounce) bounce #end
    }
  }
  scale <0.35,0.25,0.3> rotate y*15 translate y*1.5
}
#if (Overlap)
box { <-1,0.6,-1>,<1,1.9,1> pigment { rgbt 1 } hollow no_image
  interior { media { method 4 absorption rgb 0.8 } }
}
#end
