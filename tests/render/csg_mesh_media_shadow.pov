#version 3.7;
// Shadow rays cross a closed mesh inside CSG, the last one a translucent union with an absorbing medium.
global_settings { assumed_gamma 1 }
camera { location <2.2, 1.6, -3.4> look_at 0 angle 40 }
light_source { <4, 6, -5> rgb 1 }
background { rgb 0.1 }
#declare Cube = mesh {
  triangle { <-1,-1,-1>, <1,-1,-1>, <1,1,-1> } triangle { <-1,-1,-1>, <1,1,-1>, <-1,1,-1> }
  triangle { <-1,-1,1>, <1,1,1>, <1,-1,1> } triangle { <-1,-1,1>, <-1,1,1>, <1,1,1> }
  triangle { <-1,-1,-1>, <-1,1,1>, <-1,-1,1> } triangle { <-1,-1,-1>, <-1,1,-1>, <-1,1,1> }
  triangle { <1,-1,-1>, <1,-1,1>, <1,1,1> } triangle { <1,-1,-1>, <1,1,1>, <1,1,-1> }
  triangle { <-1,-1,-1>, <-1,-1,1>, <1,-1,1> } triangle { <-1,-1,-1>, <1,-1,1>, <1,-1,-1> }
  triangle { <-1,1,-1>, <1,1,1>, <-1,1,1> } triangle { <-1,1,-1>, <1,1,-1>, <1,1,1> }
  inside_vector <0.3, 0.7, 0.2>
};
difference { object { Cube } box { <0, 0, -1>, <1.5, 1.5, 1.5> } pigment { rgb <0.9,0.6,0.3> } }
intersection { object { Cube translate x*2.2 } box { <1.2, -1, -1>, <3.2, 1, 0.2> } pigment { rgb <0.3,0.6,0.9> } }
union { object { Cube translate -x*2.2 } box { <-3.2, -1, -1>, <-1.2, 0.5, 1> } hollow pigment { rgbt <1,1,1,0.7> }
  interior { media { absorption 0.4 } } }
