#version 3.7;
global_settings { assumed_gamma 1 }
#ifndef(Case) #declare Case = 0; #end
#ifndef(Reference) #declare Reference = 0; #end
#declare Far = 384400000;
#declare Near = box { <-.7,-.6,3.8>, <.7,.6,4.2> }
#declare Remote = box { <-.7,-.6,3.8>*Far, <.7,.6,4.2>*Far }
#declare Remote = object { Remote translate <1.8*Far,0,0> }
#declare Target = union { object { Near } object { Remote } }
#if(Case=1)
    #declare Near = object { Near rotate <12,23,7> translate <1,2,3> }
    #declare Remote = object { Remote rotate <12,23,7> translate <1,2,3> }
    #declare Target = union { object { Near } object { Remote } }
#end
camera {
    location 0
    right x*image_width/image_height
    #if(Case=2) spherical angle 160 80 #else angle 80 #end
    #if(Case=1) location <1,2,3> sky vrotate(y,<12,23,7>) #end
    #if(Reference)
        // Scaling the far component along camera rays preserves its complete projected silhouette.
        look_at union {
            object { Near }
            #if(Case=1)
                object { Remote translate -<1,2,3> scale 1/Far translate <1,2,3> }
            #else
                object { Remote scale 1/Far }
            #end
        }, .90
    #else
        look_at Target, .90
    #end
}
object { Near pigment { color rgb <1,0,0> } finish { emission 1 diffuse 0 } }
object { Remote pigment { color rgb <0,1,0> } finish { emission 1 diffuse 0 } }
