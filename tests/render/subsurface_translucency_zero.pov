// A translucency channel of 0 carries no subsurface transport: that channel is plain diffuse, the others unchanged.
#version 4.0;
#ifndef (T) #declare T = <1, 0, 1>; #end
#ifdef (Bad) #declare T = <1, -1, 1>; #end
#ifndef (Ext) #declare Ext = 0; #end
#ifndef (Plain) #declare Plain = 0; #end
global_settings { assumed_gamma 1 mm_per_unit 10 subsurface { method 2 samples 16, 8 spacing 0.5 } }
camera { location <0.6, 1.6, -5> look_at <0, 0.2, 0> angle 50 }
background { rgb 0 }
#if (Ext) light_source { <0.3, 3, -3>, rgb 1 } #end
sphere { 0, 1
  pigment { rgb <0.9, 0.85, 0.8> }
  finish { ambient 0 diffuse 0.8 #if (!Plain) subsurface { translucency T } #end }
  interior { ior 1.4
    #if (!Plain & !Ext)
      media { method 4 emission rgb <1, 0.6, 0.3> }
      light { emission samples 4 }
    #end
  }
}
