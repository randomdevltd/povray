// A ray that reaches max_trace_level: black before #version 4.0; from 4.0 lit without reflection and, through clear
// surfaces, continued straight. Count filtering slabs turned by Tilt degrees, so refraction shifts the emitting checker
// behind them. Depth is max_trace_level; Ior the slabs' ior; Mirror 1 gives every slab after the first reflection 1
// conserve_energy.
#version version;
#ifndef (Version) #declare Version = 3.8; #end
#version Version;
#ifndef (Depth) #declare Depth = 2; #end
#ifndef (Ior) #declare Ior = 1.5; #end
#ifndef (Mirror) #declare Mirror = 0; #end
#ifndef (Count) #declare Count = 6; #end
#ifndef (Tilt) #declare Tilt = 40; #end
global_settings { assumed_gamma 1 max_trace_level Depth adc_bailout 0 }
camera { orthographic location <0, 0, -10> look_at 0 right x * 4 up y * 3 }
union {
  #for (I, 0, Count - 1)
    box {
      <-6, -6, I * 0.5>, <6, 6, I * 0.5 + 0.2>
      pigment { rgbf <1, 0.9, 0.8, 1> }
      finish { ambient 0 diffuse 0 #if (Mirror & (I > 0)) reflection 1 conserve_energy #end }
      interior { ior Ior }
    }
  #end
  rotate y * Tilt
}
box { <-50, -50, Count + 10.25>, <50, 50, Count + 11> pigment { checker rgb 1, rgb 0.2 scale 0.5 } finish { ambient 0 diffuse 0 emission 1 } }
