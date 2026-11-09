// A volume light whose container is its own subsurface host: the host receives the light's own emission and the self
// share of its bounce, as the host's media would.
#version 4.0;
#ifndef (Bounce) #declare Bounce = 0; #end
#ifndef (Self) #declare Self = 1; #end
global_settings { assumed_gamma 1 max_trace_level 8 subsurface { method 2 samples 16, 16 spacing 0.5 } }
camera { location <0.6, 1.6, -5> look_at <0, 0.2, 0> angle 50 }
background { rgb 0 }
sphere { 0, 1
  pigment { rgb <0.9, 0.85, 0.8> }
  finish { ambient 0 diffuse 0.8 subsurface { translucency 0.4 } }
  interior { ior 1.4
    media { method 4 scattering { 1, rgb 0.5 } emission rgb <1, 0.6, 0.3> }
    light { emission samples 4 #if (Bounce > 0) bounce { passes Bounce self Self } #end }
  }
}
