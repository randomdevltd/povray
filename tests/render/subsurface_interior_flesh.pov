// A subsurface block whose flesh is looked up where light enters, shown through its interior_texture: a mesh wound
// inward, so a ray from outside meets each face's own normal from behind. Inward=0 winds the same mesh outward with the
// textures swapped, so that the same flesh is its texture.
#version 3.7;
#ifndef (Inward) #declare Inward=1; #end

global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { method 2 samples 2,0 } }
camera { orthographic location <0, 0, -4> look_at 0 right x*1.6 up y*2.4 }
background { rgb 0 }
sphere { <0,2,-4>, 0.5 pigment { rgb 0 } finish { ambient 0 diffuse 0 }
    light { pigment { rgb 1 } brightness 4 nits samples 1 } no_image no_shadow
}
#declare Wax = texture { pigment { rgb 1 } finish { ambient 0 diffuse 1 subsurface {
    method 2 translucency 1 pigment { rgb 0.8 } volume_sampling 1 } } }
#declare Black = texture { pigment { rgb 0 } finish { ambient 0 diffuse 0 } }
// a quad whose corners run so that its normal points out of the block, or into it
#macro Quad(A, B, C, D)
    #if (Inward) triangle { A, B, C } triangle { A, C, D } #else triangle { A, C, B } triangle { A, D, C } #end
#end
#declare L = <-0.5, -1, -0.5>;
#declare H = <0.5, 1, 0.5>;
mesh {
    Quad(<L.x,L.y,L.z>, <L.x,H.y,L.z>, <H.x,H.y,L.z>, <H.x,L.y,L.z>)
    Quad(<H.x,L.y,H.z>, <H.x,H.y,H.z>, <L.x,H.y,H.z>, <L.x,L.y,H.z>)
    Quad(<L.x,L.y,H.z>, <L.x,H.y,H.z>, <L.x,H.y,L.z>, <L.x,L.y,L.z>)
    Quad(<H.x,L.y,L.z>, <H.x,H.y,L.z>, <H.x,H.y,H.z>, <H.x,L.y,H.z>)
    Quad(<L.x,H.y,L.z>, <L.x,H.y,H.z>, <H.x,H.y,H.z>, <H.x,H.y,L.z>)
    Quad(<L.x,L.y,H.z>, <L.x,L.y,L.z>, <H.x,L.y,L.z>, <H.x,L.y,H.z>)
    inside_vector y
    #if (Inward) texture { Black } interior_texture { Wax } #else texture { Wax } interior_texture { Black } #end
    // the point budget is set here: without it the spacing follows the texture's diffusion length, not the interior_texture's
    interior { ior 1.3 } subsurface { domain_cells 32 }
}
