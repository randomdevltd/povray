#version 3.7;
#ifndef (Check) #declare Check = 0; #end
global_settings { assumed_gamma 1 }
camera { location -5*z look_at 0 }
#switch (Check)
  #case (0)
    #declare tags = 1;
    #declare filter_tags = 2;
    #declare front_filter_tags = 3;
    #declare back_filter_tags = 4;
    #declare any = 5;
    #declare none = 6;
    #macro LegacySum(any, none) (any + none) #end
    #if (LegacySum(2, 3) != 5) #error "Legacy macro parameter names rejected" #end
    #if (tags + filter_tags + front_filter_tags + back_filter_tags + any + none != 21)
      #error "Legacy identifiers changed value"
    #end
    sphere { 0, tags pigment { rgb 1 } finish { emission 1 diffuse 0 } }
    #break
  #case (1) sphere { 0, 1 tags { "new" } } #break
  #case (2) camera { filter_tags { "new" } } #break
#end
