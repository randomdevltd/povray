#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (Scene) #declare Scene = 0; #end
#if (Scene = 0)
    #include "phot_met_glass.pov"
#else
    #include "glassthing.pov"
#end
#if (Method = 2)
    global_settings { photons { method 2 quality PhotonQuality } }
#end
