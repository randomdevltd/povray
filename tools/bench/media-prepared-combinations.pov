// Compare Mode 0/1 (stack/equivalent), then Mode 2/3 (overlapping stacks/equivalents).
#version 4.0;
#ifndef (Wrapped) #declare Wrapped = 0; #end
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Method) #declare Method = 4; #end
#ifndef (CellSize) #declare CellSize = 0.12; #end
#ifndef (Samples) #declare Samples = 12; #end
#if (Mode < 0 | Mode > 3) #error "Mode must be 0, 1, 2 or 3." #end
global_settings { assumed_gamma 1 max_trace_level 12 }
camera { orthographic location <0, 0, -6> look_at 0 right x*4.8 up y*3.2 }
background { rgb <0.3, 0.4, 0.5> }
light_source { <-10, 15, -20> rgb 1 parallel point_at 0 media_attenuation on }
#declare Field = function(x,y,z) { 0.5 + 0.08*x + 0.12*y + 0.06*z };
#macro Coefficients(Weight)
  absorption rgb <0.18, 0.12, 0.06>*Weight
  emission rgb <0.04, 0.02, 0.01>*Weight
  scattering { 1, rgb <0.24, 0.30, 0.36>*Weight extinction 1 }
#end
#macro Variable(Weight, Width, Offset)
  media {
    method Method
    #if (Method = 4) resolution Width #end
    intervals 1 samples Samples jitter 0
    Coefficients(Weight)
    density { function { Field(x,y,z) + Offset } }
  }
#end
#macro Container(Centre)
  box { Centre - <1,1,1>, Centre + <1,1,1>
    hollow pigment { rgbt 1 }
    interior {
      #if (mod(Mode,2) = 0)
        Variable(0.4, CellSize, 0)
        Variable(0.6, CellSize/2, 0)
        media { method 3 intervals 1 samples Samples jitter 0 Coefficients(0.2) }
      #else
        Variable(1, CellSize/2, 0.2)
      #end
    }
  }
#end
#if (Wrapped) union { sphere { <100,100,100>, 0.1 pigment { rgb 1 } } #end
#if (Mode < 2)
  Container(<0,0,0>)
#else
  Container(<-0.4,0,-0.3>)
  Container(<0.4,0,0.3>)
#end

#if (Wrapped) } #end
