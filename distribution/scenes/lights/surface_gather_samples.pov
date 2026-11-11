// Fixed per-node sampling over thin translucent capsules and nearby volume lights.
#version 3.7;
#ifndef(Power) #declare Power=1; #end
#ifndef(S) #declare S=1; #end
#ifndef(Gather) #declare Gather=8; #end
#ifndef(WithBounce) #declare WithBounce=1; #end
#ifndef(Export) #declare Export=0; #end
#ifndef(Cells) #declare Cells=512; #end
#ifndef(SS) #declare SS=8; #end
#ifndef(Nearby) #declare Nearby=40; #end
global_settings { assumed_gamma 1 mm_per_unit 1/S subsurface { method 2 samples SS,SS spacing 0.5 } }
camera { orthographic location <0,0,-3>*S look_at 0 right x*2*S up y*1.5*S }
background { rgb 0.01 }
#for(K,-1,1)
#declare O=<K*0.5,0,0>;
sphere_sweep { cubic_spline 4, <-0.1,-1,0>,0.035, <0,-0.5,0>,0.035, <0,0.5,0>,0.07, <0.1,1,0>,0.035
 pigment { rgb <0.8,0.5,0.3> } finish { ambient 0 diffuse 1 subsurface { translucency <0.04,0.07,0.12> } }
 interior { ior 1.4 }
 light { #if (Export) emission #end bounce { passes 1 } samples Gather domain_cells Cells brightness WithBounce }
 translate O scale S }
difference { sphere { 0,0.055 } sphere { 0,0.048 } plane { -y,0.025 }
 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
 interior { ior off light { emission samples 8 domain_cells 32 attenuation { cutoff 0 } }
 media { method 4 domain_cells 128 mix add priority 3 emission <0.1,0.8,1>*200*Power
 density { function { 0.1+0.9*pow(0.5+0.5*sin(500*x)*sin(500*y)*sin(500*z),8) } } } }
 translate <0,0.42,0>+O scale S }
#end
#for(K,0,Nearby-1)
#declare T=K*2*pi/Nearby;
sphere { <sin(T)*0.8,cos(T)*0.6,-0.3> 0.015 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
 interior { light { brightness 300*Power samples 4 domain_cells 16 attenuation { cutoff 0 } } } scale S }
#end

box { <-1,-0.62,-0.5>, <1,-0.6,0.8> pigment { rgb 1 } finish { ambient 0 diffuse 1 } scale S }
