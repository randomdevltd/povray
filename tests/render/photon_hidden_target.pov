// A clear photon target hidden from the camera or from shadows still receives photons. Declare=Case:
// 0 plain, 1 no_image, 2 no_shadow, 3 no_shadow + pass_through off, 4-7 opaque non-target panel in front of it.
#version 3.7;

#ifndef (Case) #declare Case = frame_number; #end
#ifndef (Spacing) #declare Spacing = 0.02; #end

global_settings { assumed_gamma 1 max_trace_level 10 photons { spacing Spacing } }

camera { location <0.12, 0.09, -0.16> look_at <0, 0, 0.02> angle 60 }

light_source { <0, 0, -0.095> rgb 1 }

plane { z, 0.1 pigment { rgb 0.8 } }

box {
    -0.035, 0.035
    pigment { rgbf 1 }
    finish { reflection 0.05 }
    interior { ior 1.5 }
    photons { target 0.005 refraction on reflection on #if (Case = 3) pass_through off #end }
    #if (Case = 1) no_image #end
    #if (Case = 2 | Case = 3) no_shadow #end
}

#if (Case >= 4)
    box {
        <-0.05, -0.05, -0.062>, <0.05, 0.05, -0.06>
        pigment { rgb 0.5 }
        #switch (Case)
            #case (4) no_shadow #break
            #case (5) photons { pass_through } #break
            #case (7) no_shadow photons { pass_through off } #break
        #end
    }
#end
