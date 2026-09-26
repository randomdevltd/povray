// Radiosity stress: a lawn of thin blades between posts and canopies, a sun and many small fading lamps; see doc/PERF.md.
#version 3.7;
#ifndef (Count) #declare Count = 60; #end
#ifndef (ErrorBound) #declare ErrorBound = 0.6; #end
#ifndef (Recursion) #declare Recursion = 1; #end
#ifndef (PretraceStart) #declare PretraceStart = 0.16; #end
#ifndef (PretraceEnd) #declare PretraceEnd = 0.02; #end
#ifndef (LowErrorFactor) #declare LowErrorFactor = 0.5; #end
#ifndef (NearestCount) #declare NearestCount = 5; #end
#ifndef (MinimumReuse) #declare MinimumReuse = 0.015; #end
#ifndef (Lamps) #declare Lamps = 24; #end
#ifndef (Blades) #declare Blades = 40000; #end
#ifndef (Radiosity) #declare Radiosity = 1; #end

global_settings {
  assumed_gamma 1.0
  max_trace_level 10
  #if (Radiosity)
  radiosity {
    pretrace_start PretraceStart
    pretrace_end PretraceEnd
    count Count
    error_bound ErrorBound
    recursion_limit Recursion
    low_error_factor LowErrorFactor
    nearest_count NearestCount
    minimum_reuse MinimumReuse
    always_sample off
  }
  #end
}
#default { finish { ambient 0 diffuse 0.7 } }

camera { perspective location <0, 1.6, -9> look_at <0, 1.1, 0> angle 70 right x * image_width / image_height }

light_source {
  <-300, 260, 200>, rgb <1.3, 1.1, 0.9>
  parallel point_at <0, 0, 0>
  area_light <8, 0, 0>, <0, 0, 8>, 9, 9
  adaptive 1 jitter circular orient
}

sky_sphere { pigment { gradient y color_map { [0 rgb <0.9, 0.9, 1.0>] [1 rgb <0.2, 0.35, 0.8>] } } }

plane { y, 0 pigment { rgb <0.25, 0.2, 0.12> } }

#declare R = seed(7);
mesh {
  #for (I, 1, Blades)
    #local P = <-12 + 24 * rand(R), 0, -6 + 20 * rand(R)>;
    #local A = rand(R) * 2 * pi;
    #local W = 0.012 + 0.01 * rand(R);
    #local H = 0.12 + 0.25 * rand(R);
    #local L = <0.1 * (rand(R) - 0.5), H, 0.1 * (rand(R) - 0.5)>;
    triangle { P - W * <cos(A), 0, sin(A)>, P + W * <cos(A), 0, sin(A)>, P + L }
  #end
  pigment { rgb <0.15, 0.45, 0.12> }
}

#for (I, 0, 11)
  #local P = <-10 + 20 * rand(R), 0, -1 + 14 * rand(R)>;
  #local H = 3 + 3 * rand(R);
  cylinder { P, P + y * H, 0.15 + 0.15 * rand(R) pigment { rgb <0.35, 0.22, 0.14> } normal { bumps 0.4 scale 0.05 } }
  union {
    #for (J, 0, 30)
      sphere { P + y * H + <rand(R) - 0.5, rand(R) - 0.3, rand(R) - 0.5> * 3, 0.3 + 0.4 * rand(R) }
    #end
    pigment { rgb <0.2, 0.5, 0.25> }
  }
#end

disc { <3, 0.02, 1>, y, 1.8 pigment { rgbf <0.8, 0.9, 1, 0.9> } finish { reflection 0.3 } interior { ior 1.33 } }

#for (I, 1, Lamps)
  #local P = <-10 + 20 * rand(R), 0.3 + 2 * rand(R), -4 + 16 * rand(R)>;
  #local C = <0.5 + 0.5 * rand(R), 0.4 + 0.4 * rand(R), 0.3 + 0.7 * rand(R)>;
  light_source {
    P, rgb C
    fade_distance 0.8 fade_power 2
    looks_like { sphere { 0, 0.05 pigment { rgb C } finish { emission 3 diffuse 0 } } }
  }
#end
