#version 3.7;
global_settings { assumed_gamma 1 max_trace_level 3 }
camera { location <0,1.8,-8> look_at <0,0.8,0.5> angle 48 }
light_source { <-4,8,-5> rgb 1 }
background { rgb <0.1,0.15,0.2> }
plane { y,0 pigment { rgb 0.3 } finish { reflection 0.25 } }
#declare S = seed(7301);
#for (J,0,19)
 #for (I,0,19)
  #declare P = <(I-9.5)*0.22,0,(J-9.5)*0.24>;
  #declare H = 0.7+0.6*rand(S);
  #declare B = <0.25*(rand(S)-0.5),0,0.25*(rand(S)-0.5)>;
  sphere_sweep { cubic_spline 6,
   P-<0,H/3,0>,0.012, P,0.012,
   P+<0,H/3,0>+B,0.012, P+<0,2*H/3,0>+2*B,0.012,
   P+<0,H,0>+4*B,0.012, P+<0,4*H/3,0>+7*B,0.012
   pigment { rgb <0.2+rand(S)*0.2,0.5,0.1> } finish { phong 0.3 }
  }
 #end
#end

#ifdef (ParseOnly) #error "Parse-only measurement complete" #end
