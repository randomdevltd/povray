// Modes: 0 oversized grid, 1 classic overlap, 2 long segment, 3 photon propagation.
#version 4.0;
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Method) #declare Method = 4; #end
#ifndef (Samples) #declare Samples = 12; #end
#ifndef (ClassicOverlap) #declare ClassicOverlap = 1; #end
#ifndef (SegmentLength) #declare SegmentLength = 160; #end
#ifndef (CellSize)
  #if (Mode = 0) #declare CellSize = 0.005;
  #else #declare CellSize = 0.1; #end
#end
#if (Mode < 0 | Mode > 3) #error "Mode must be 0, 1, 2 or 3." #end
global_settings {
  assumed_gamma 1 max_trace_level 8
  #if (Mode = 3) photons { spacing 0.25 media 4 } #end
}
#if (Mode = 3)
  light_source { <-3,5,-5> rgb 1 photons { refraction on reflection off } }
#end
background { rgb <0.6,0.7,0.8> }
#if (Mode = 2)
  #declare Extent = <0.05,0.05,SegmentLength/2>;
  #declare Strength = 2/SegmentLength;
  camera { orthographic location <0,0,-SegmentLength/2-1> look_at 0 right x*0.12 up y*0.08 }
#else
  #declare Extent = <1,1,1>;
  #declare Strength = 1;
  camera { orthographic location <0,0,-4> look_at 0 right x*3.6 up y*2.4 }
#end
box { -Extent, Extent hollow pigment { rgbt 1 }
  #if (Mode = 3) photons { target refraction on reflection off } #end
  interior {
    #if (Mode = 3) ior 1.1 #end
    media {
      method Method
      #if (Method = 4) resolution CellSize #end
      intervals 1 samples Samples jitter 0
      absorption rgb <0.4,0.2,0.1>*Strength
      emission rgb <0.03,0.05,0.08>*Strength
      #if (Mode = 3) scattering { 1, rgb 0.01 } #end
      density { function { 0.6 + 0.1*y } }
    }
    #if (Mode = 1)
      media {
        #if (ClassicOverlap) method 3
        #else
          method Method
          #if (Method = 4) resolution CellSize #end
        #end
        intervals 1 samples Samples jitter 0
        absorption rgb <0.1,0.2,0.3>
        density { function { 0.5 + 0.1*x } }
      }
    #end
  }
}
