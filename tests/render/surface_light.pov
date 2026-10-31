#version 3.7;
#ifndef (SceneVersion) #declare SceneVersion = 3.7; #end
#version SceneVersion;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Samples) #declare Samples = 32; #end
#ifndef (Units) #declare Units = 0; #end
#ifndef (Alias) #declare Alias = 0; #end
#ifndef (Bounce) #declare Bounce = 0; #end
#ifndef (Passive) #declare Passive = 0; #end
#ifndef (Volume) #declare Volume = 0; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0,5,-1> look_at 0 right x*2 up z*2 }
background { rgb 0 }
plane { y,0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#declare Source = pigment { rgb 1 };
#switch (Case)
  #case (1)
    #declare Shape = box { <-0.7,1.4,-0.04>, <0.7,2.1,0.04> };
    #break
  #case (2)
    #declare Shape = box { <-0.8,1.5,-0.03>, <0.8,1.55,0.03> };
    #break
  #case (3)
    #declare Shape = difference {
      sphere { 0,0.35 }
      box { <-0.1,-0.5,-0.5>, <0.5,0.5,0.5> }
      translate y*1.5
    };
    #break
  #case (4)
    #declare Shape = mesh {
      triangle { <-0.5,1.5,-0.5>, <0.5,1.5,0.5>, <0.5,1.5,-0.5> }
      triangle { <-0.5,1.5,-0.5>, <-0.5,1.5,0.5>, <0.5,1.5,0.5> }
    };
    #break
  #case (5)
    #declare Source = pigment { rgb <0.8,0.4,0.2> };
    #declare Shape = sphere { <0,1.5,0>,0.25 };
    #break
  #case (6)
    #declare Shape = sphere { 0,1 scale <0.15,0.35,0.25> rotate z*20 translate y*1.5 };
    #break
  #case (7)
    #declare Shape = union {
      sphere { <-0.2,1.5,0>,0.25 }
      sphere { <0.2,1.5,0>,0.25 }
    };
    #break
  #case (8)
    #declare Shape = difference {
      box { <-0.5,1,-0.5>, <0.5,2,0.5> }
      box { <-0.35,1.1,-0.7>, <0.35,1.3,0.7> }
      intersection {
        cylinder { <0,1.3,-0.7>, <0,1.3,0.7>, 0.35 }
        box { <-0.4,1.3,-0.9>, <0.4,1.7,0.9> }
      }
    };
    #break
  #case (9)
    #declare Shape = union {
      sphere { <-0.3,1.5,0>,0.2 }
      portal { sphere { <0.3,1.5,0>,0.2 } to { translate x*5 } far off }
    };
    #break
  #else
    #declare Shape = sphere { <0,1.5,0>,0.25 };
#end
object { Shape no_image
  pigment { rgb <0.4,0.2,0.1> }
  finish { ambient 0 emission #if (Passive) 0 #else 2 #end diffuse 0 }
  light {
    #if (Alias) emission #else #if (Case = 5) pigment { Source } #end #end
    brightness #if (Units = 1) (8*4*pi*pi*0.25*0.25) lumens
    #else 8 #if (Units != 2) nits #end #end
    samples Samples
    #if (Bounce > 0) bounce #if (Bounce > 1) { passes #if (Bounce = 2) 1 #else 3 #end } #end #end
  }
}

#if (Bounce = 4)
  sphere { <0.8,1.5,0>,0.2 no_image
    pigment { rgb <0.2,0.5,1> }
    finish { ambient 0 emission 1 diffuse 0.5 }
    light { bounce { passes 1 } samples Samples }
  }
#end

#if (Volume)
  sphere { <0,0.6,0>,0.08 pigment { rgbt 1 } hollow no_image
    interior { light { brightness 8 samples Samples } }
  }
#end
