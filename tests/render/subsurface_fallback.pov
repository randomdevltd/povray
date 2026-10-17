// Former method 1 fallbacks by Case: 0 union overlap, 1 open mesh, 2 disc, 3 wall under the probe floor, 4 plane, 5 light group,
// 6 wide ring, 7 shared face, 8 no absorption, 9 photons, 10 empty cell, 11 rods, 12 step, 13 radiosity, 14 layers, 15-17 solids.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Diffuse) #declare Diffuse = 16; #end
#ifndef (Spacing) #declare Spacing = (Case = 6 ? 100 : Case = 10 ? 200 : Case = 4 ? 50 : 1); #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples Diffuse, 4 method Method spacing Spacing radiosity (Case = 13) }
  #if (Case = 9) photons { count 4000 } #end
  #if (Case = 13) radiosity { pretrace_start 0.08 pretrace_end 0.04 count 20 recursion_limit 1 } #end
}

camera { location <0, 1.5, -4> look_at <0, 0.4, 0> right x * image_width / image_height angle 45 }
#declare Sun = light_source { <-2, 5, -3> rgb 1.3 }
light_source { Sun }

#declare Skin = material {
  texture {
    pigment { rgb (Case = 8 ? 1 : <0.8, 0.4, 0.25>) }
    finish { ambient 0 diffuse (Case = 8 ? 1 : 0.8) subsurface { translucency (Case = 6 | Case = 10 ? 0.4 : <3, 2, 1.5>) } }
  }
  interior { ior 1.4 }
}

#switch (Case)
  #case (0)
    union { box { <-0.6, 0, -0.6>, <0.2, 1, 0.6> } box { <-0.2, 0, -0.6>, <0.6, 0.8, 0.6> } material { Skin } }
    #break
  #case (1)
    mesh {
      triangle { <-0.8, 0, -0.8>, <0.8, 0.3, -0.8>, <0.8, 0, 0.8> }
      triangle { <-0.8, 0, -0.8>, <0.8, 0, 0.8>, <-0.8, 0.3, 0.8> }
      inside_vector y
      material { Skin }
    }
    #break
  #case (2)
    disc { <0, 0.3, 0>, <0, 1, -0.4>, 0.8 material { Skin } }
    #break
  #case (3)
    box { <-0.6, 0, 0>, <0.6, 1, 1e-7> material { Skin } }
    #break
  #case (4)
    plane { y, 0 material { Skin } }
    #break
  #case (5)
    merge {
      box { <-0.6, 0, -0.6>, <0, 1, 0.6> }
      light_group { light_source { Sun translate x } box { <0, 0, -0.6>, <0.6, 1, 0.6> } }
      material { Skin }
    }
    #break
  #case (6)
    box { <-400, -1, -2>, <400, 0, 400> material { Skin } }
    #break
  #case (7)
    merge { box { <-0.6, 0, -0.6>, <0.6, 0.5, 0.6> } cylinder { <0, 0.5, 0>, <0, 0.8, 0>, 0.3 } material { Skin } }
    #break
  #case (9)
    light_source { <1, 3, -2> rgb 0.6 photons { refraction on } }
    sphere { <0.9, 1.2, -0.8>, 0.25 pigment { rgbf 1 } interior { ior 1.5 } photons { target refraction on } }
    merge {
      box { <-0.6, 0, -0.6>, <0, 1, 0.6> }
      box { <0, 0, -0.6>, <0.6, 1, 0.6> photons { collect off } }
      material { Skin }
    }
    #break
  #case (10)
    box { <-30, -1, -2>, <30, 0, 30> material { Skin } }
    #break
  #case (11)
    #for (I, 0, 4) cylinder { <-1, 0.15 + I * 0.2, I * 0.1>, <1, 0.15 + I * 0.2, I * 0.1>, 0.04 material { Skin } } #end
    #break
  #case (12)
    merge { box { <-0.8, 0, -0.6>, <0.8, 0.3, 0.6> } box { <-0.8, 0.3, 0.1>, <0.8, 0.9, 0.6> } material { Skin } }
    #break
  #case (15)
    mesh2 {
      vertex_vectors { 24,
        <-0.6,0,-0.6>,<0.6,0,-0.6>,<0.6,1,-0.6>,<-0.6,1,-0.6>, <-0.6,0,0.6>,<0.6,0,0.6>,<0.6,1,0.6>,<-0.6,1,0.6>,
        <-0.6,0,-0.6>,<-0.6,1,-0.6>,<-0.6,1,0.6>,<-0.6,0,0.6>, <0.6,0,-0.6>,<0.6,1,-0.6>,<0.6,1,0.6>,<0.6,0,0.6>,
        <-0.6,0,-0.6>,<0.6,0,-0.6>,<0.6,0,0.6>,<-0.6,0,0.6>, <-0.6,1,-0.6>,<0.6,1,-0.6>,<0.6,1,0.6>,<-0.6,1,0.6> }
      face_indices { 12, <0,1,2>,<0,2,3>, <4,6,5>,<4,7,6>, <8,10,9>,<8,11,10>, <12,13,14>,<12,14,15>, <16,18,17>,<16,19,18>,
        <20,21,22>,<20,22,23> }
      inside_vector y
      material { Skin }
    }
    #break
  #case (16)
    intersection { box { <-0.6, 0, -0.6>, <0.6, 1, 0.6> } disc { <0, 0.7, 0>, y, 2 } material { Skin } }
    #break
  #case (17)
    merge { box { <-0.6, 0, -0.6>, <-0.02, 1, 0.6> } box { <0.02, 0, -0.6>, <0.6, 0.6, 0.6> } material { Skin } }
    #break
  #case (14)
    #for (I, 0, 3)
      box { <-0.8, I * 0.04, -0.6>, <0.8, I * 0.04 + 0.02, 0.6> rotate x * -20 material { Skin } }
    #end
    #break
  #else
    plane { y, 0 pigment { rgb 0.4 } }
    box { <-0.6, 0, -0.6>, <0.6, 1, 0.6> material { Skin } }
#end
