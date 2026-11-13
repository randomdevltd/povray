// Cubic and b-spline sphere sweeps must report the crossings of every ray, at any scene scale S, and agree with their
// own inside test. Check=1 traces rays along and across a thin tapered tube, and square to the spine of a straight
// one, and stops the parse where trace() misses material that inside() finds on the ray. Check=0 renders tubes that
// host a volume light under method 2 subsurface: Case 0 a row of thin stalks crossed by a buried slab, Case 1 a stalk
// whose bulb is carved by a socket holding a shell light, Case 2 the same on a b-spline merged with a closed mesh.
#version 4.0;
#ifndef (Check) #declare Check=0; #end
#ifndef (Case) #declare Case=0; #end
#ifndef (S) #declare S=1; #end
#macro Tube(Kind, X)
 sphere_sweep { #if (Kind) b_spline #else cubic_spline #end 5,
  <X-0.0015,-0.016,0>*S,0.0008*S, <X,-0.01,0>*S,0.0008*S, <X+0.0005,0,0>*S,0.0005*S, <X,0.008,0>*S,0.0015*S, <X,0.012,0>*S,0.0015*S }
#end
#if (Check)
 #declare Rays=400; #declare Steps=240; #declare R=seed(7); #declare Missed=0; #declare Early=0;
 #for (Kind,0,1)
  #declare Shape=Tube(Kind,0)
  #for (I,1,Rays)
   #declare Aim=<0.0005*rand(R),-0.01+0.018*rand(R),0>+(<rand(R),rand(R),rand(R)>-0.5)*0.003;
   #declare Dir=vnormalize(<(rand(R)-0.5)*(mod(I,2)?0.5:4),(rand(R)<0.5?-1:1),(rand(R)-0.5)*(mod(I,2)?0.5:4)>);
   #declare From=(Aim-Dir*0.03)*S; #declare Step=0.06*S/Steps; #declare First=-1;
   #for (K,1,Steps) #if (First<0) #if (inside(Shape,From+Dir*K*Step)) #declare First=K; #end #end #end
   #declare Normal=<0,0,0>; #declare Hit=trace(Shape,From,Dir,Normal);
   #if (First>0)
    #if (vlength(Normal)=0) #declare Missed=Missed+1;
    #elseif (vlength(Hit-From)>First*Step*(1+1e-9) | vlength(Hit-From)<(First-1)*Step*(1-1e-9)) #declare Early=Early+1; #end
   #end
  #end
 #end
 // A ray square to the spine: its entry and exit are stationary at one sphere, a double root for a solver in u alone.
 #declare Shape=sphere_sweep { cubic_spline 4, <-1,0,0>*S,0.1*S, <0,0,0>*S,0.1*S, <1,0,0>*S,0.1*S, <2,0,0>*S,0.1*S }
 #for (I,1,40)
  #declare From=<0.013+0.0241*I,0,0>*S; #declare Normal=<0,0,0>; #declare Hit=trace(Shape,From,y,Normal);
  #if (vlength(Normal)=0) #declare Missed=Missed+1; #elseif (abs(vlength(Hit-From)/S-0.1)>1e-9) #declare Early=Early+1; #end
 #end
 #if (Missed+Early>0) #error concat("sphere sweep at scale ",str(S,0,-1),": ",str(Missed,0,0)," rays missed material on them, ",str(Early,0,0)," crossings lie off the first material") #end
#end
global_settings { assumed_gamma 1 mm_per_unit 1000/S subsurface { method 2 samples 8,8 } }
background { rgb 0 }
camera { location <0,0.02,-0.06>*S look_at <0,0,0> angle 40 }
light_source { <0.05,0.08,-0.06>*S rgb 0.4 }
#declare Wax=texture { pigment { rgb <0.95,0.85,0.7> } finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { method 2 translucency <3,2,1> colour rgb 0.95 } } }
#macro Lamp(Lumens) pigment { rgbt 1 } hollow interior { ior off light { pigment { rgb <1,0.6,0.25> } brightness scale_lumens(Lumens*0.02, 1/S) lumens samples 4 } } #end
#macro Bulb(Kind)
 sphere_sweep { #if (Kind) b_spline #else cubic_spline #end 7, <-0.03,-0.012,0>,0.0012, <-0.02,-0.01,0>,0.0012, <-0.01,-0.004,0.002>,0.0008,
  <0,0,0>,0.0006, <0.008,0.004,0>,0.0012, <0.012,0.006,0>,0.006, <0.016,0.008,0>,0.006 #if (!Kind) tolerance 0.0000015 #end }
#end
#declare Eye=<0.012,0.006,-0.003>;
#switch (Case)
#case (0)
 union { #for (I,0,5) Tube(0, I*0.005-0.0125) #end texture { Wax } interior { ior 1.45 } }
 box { <-0.014,-0.0002,-0.0002>,<0.014,0.0002,0.0052> Lamp(0.5) scale S }
#break
#case (1)
 difference { Bulb(0) sphere { Eye,0.0042 } texture { Wax } interior { ior 1.45 } scale S }
 difference { sphere { 0,0.0037 } sphere { 0,0.0035 } plane { z,0 } translate Eye Lamp(0.3) scale S }
 sphere { Eye,0.0034 pigment { rgb 1 } scale S }
#break
#case (2)
 difference {
  merge { Bulb(1)
   mesh2 { vertex_vectors { 6, <-0.004,0,0>,<0.004,0,0>,<0,-0.004,0>,<0,0.004,0>,<0,0,-0.002>,<0,0,0.002> }
    face_indices { 8, <0,2,4>,<2,1,4>,<1,3,4>,<3,0,4>,<2,0,5>,<1,2,5>,<3,1,5>,<0,3,5> } inside_vector <0.13,0.27,1> rotate 20*z translate Eye-0.0015*z } }
  union { sphere { 0,0.0042 } cone { 0,0.003,-0.006*z,0.004 } translate Eye }
  texture { Wax } interior { ior 1.45 } scale S }
 difference { sphere { 0,0.0037 } sphere { 0,0.0035 } plane { z,0 } translate Eye Lamp(0.3) scale S }
#break
#end
