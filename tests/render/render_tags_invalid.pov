#version 4.0;
#ifndef (Check) #declare Check = 0; #end
global_settings { assumed_gamma 1 }
camera { location -5*z look_at 0 }
#switch (Check)
  #case (0) sphere { 0, 1 tags { "bad*tag" } } #break
  #case (1) camera { filter_tags {} } #break
  #case (2) camera { filter_tags { "a" & } } #break
  #case (3) camera { filter_tags { ("a" | "b" } } #break
  #case (4) camera { filter_tags { "a", "b" } } #break
  #case (5) camera { filter_tags { any none } } #break
  #case (6) #declare any = "a"; camera { filter_tags { any } } #break
  #case (7) #declare none = "a"; camera { filter_tags { none } } #break
  #case (8)
    #macro ShadowAny(any) camera { filter_tags { any } } #end
    ShadowAny("a")
    #break
  #case (9)
    #macro ShadowNone(none) camera { filter_tags { none } } #end
    ShadowNone("a")
    #break
#end
