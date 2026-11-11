#version 3.7;
global_settings { assumed_gamma 1 max_trace_level 3 }
camera { location <0,2,-7> look_at <0,0.8,0> angle 48 }
light_source { <-4,8,-5> rgb 1 }
background { rgb <0.1,0.15,0.2> }
plane { y,0 pigment { rgb 0.3 } finish { reflection 0.25 } }
#for (I,-1,1)
 sphere_sweep { cubic_spline 6,
  <I*1.5,-1,0>,0.3, <I*1.5,0,0>,0.3,
  <I*1.5+0.25,0.7,0.2>,0.3, <I*1.5-0.2,1.4,0.4>,0.3,
  <I*1.5+0.3,2,0.1>,0.3, <I*1.5,2.6,0>,0.3
  pigment { rgb <0.3,0.6,0.2> } finish { phong 0.3 }
 }
#end

#ifdef (ParseOnly) #error "Parse-only measurement complete" #end
