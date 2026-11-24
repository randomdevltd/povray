// A volume light buried just under the skin of its host, the forest eyestalk's vein lamp in each host shape: the lamp is
// the host's outer hundredth, from a millionth of its size (Skin) inside the surface, so some paths from it start where
// a solver's absolute cut, a root left at its closed form's accuracy or an inside test's fixed band loses the crossing
// just ahead, and the exit search stops the render. S scales the scene with mm_per_unit, so the image is the same at
// any S.
// Case: 0 torus, 1 cylinder, 2 cone, 3 prism, 4 surface of revolution, 5 quartic torus, 6 sphere,
// 7 merge of a cylinder, a cone and a torus, 8 cubic sphere sweep (an eyestalk with a bulb).
#version 4.0;
#ifndef (Case) #declare Case=0; #end
#ifndef (S) #declare S=1; #end
#ifndef (Skin) #declare Skin=1e-6; #end
// the per-pixel paths: the exit search's roots are the walk's, which the cloud points also take, and pixels average its noise
global_settings { assumed_gamma 1 mm_per_unit 1000/S subsurface { method 2 samples 8,8 internal_paths on } }
background { rgb 0 }
camera { location <0,0.03,-0.07>*S look_at <0,0,0> angle 40 }
light_source { <0.05,0.08,-0.06>*S rgb 0.2 }
#declare Wax=texture { pigment { rgb <0.95,0.85,0.7> } finish { diffuse 0.8 specular 0.2 roughness 0.02 subsurface { method 2 translucency <3,2,1> colour rgb 0.95 } } }
#macro Lamp(Lumens) pigment { rgbt 1 } hollow interior { ior off light { pigment { rgb <1,0.6,0.25> } brightness scale_lumens(Lumens*0.02, 1/S) lumens samples 4 } } #end
// Each host at 1 - K of its size, kept inside the full one; the lamp is the shell between K = Skin and K = 0.01.
#macro Shape(K)
 #switch (Case)
 #case (0) torus { 0.012, 0.005*(1-K) } #break
 #case (1) cylinder { <-0.015*(1-K),0,0>, <0.015*(1-K),0,0>, 0.006*(1-K) } #break
 #case (2) cone { <-0.015*(1-K),0,0>, 0.003*(1-K), <0.015*(1-K),0,0>, 0.009*(1-K) } #break
 #case (3) prism { linear_sweep linear_spline -0.005*(1-K), 0.005*(1-K), 6,
   <-0.016,-0.012>*(1-K), <0.018,-0.014>*(1-K), <0.004,0>*(1-K), <0.014,0.016>*(1-K), <-0.012,0.014>*(1-K), <-0.016,-0.012>*(1-K) } #break
 #case (4) sor { 6, <0.004,-0.016>*(1-K), <0.008,-0.012>*(1-K), <0.004,0>*(1-K), <0.009,0.01>*(1-K), <0.002,0.014>*(1-K), <0.001,0.018>*(1-K) } #break
 #case (5) // (x^2 + y^2 + z^2 + R^2 - r^2)^2 - 4 R^2 (x^2 + z^2), axis y, modelled in units of R
  #local Rm=0.42*(1-K); #local Q=1-Rm*Rm;
  intersection { quartic { <1,0,0,0, 2,0,0, 2,0, 2*Q-4, 0,0,0,0,0,0,0,0,0,0, 1,0,0, 2,0, 2*Q, 0,0,0,0, 1,0, 2*Q-4, 0, Q*Q> sturm }
   box { <-1.5,-0.5,-1.5>, <1.5,0.5,1.5> } scale 0.012 } #break
 #case (6) sphere { <0.001,-0.002,0.0005>, 0.01*(1-K) } #break
 #case (7) merge { cylinder { <-0.015*(1-K),0,0>, <0.015*(1-K),0,0>, 0.004*(1-K) } cone { <0,-0.012*(1-K),0>, 0.002*(1-K), <0,0.012*(1-K),0>, 0.006*(1-K) } torus { 0.012, 0.003*(1-K) } } #break
 #case (8) sphere_sweep { cubic_spline 6, <-0.03,-0.004,0>,0.003*(1-K), <-0.015,0,0>,0.003*(1-K), <0,0.002,0>,0.0025*(1-K), <0.008,0.004,0>,0.003*(1-K),
   <0.012,0.005,0>,0.007*(1-K), <0.016,0.006,0>,0.007*(1-K) } #break
 #end
#end
object { Shape(0) texture { Wax } interior { ior 1.45 } scale S }
difference { object { Shape(Skin) } object { Shape(0.01) } Lamp(0.5) scale S }
