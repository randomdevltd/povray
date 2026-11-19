// Two bricks in one subsurface body, each with its own flesh looked up where light enters.
// Layout: 0 one texture on the union, 1 a copy of the same texture on each brick, 2 a different flesh on each brick,
// 3 one skin texture on the union, 4 a different skin on each brick.
#version 3.7;
#ifndef (Layout) #declare Layout=1; #end
#ifndef (LeftFlesh) #declare LeftFlesh=0.8; #end
#ifndef (LeftSkin) #declare LeftSkin=0.9; #end

global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { method 2 samples 2,0 } }
camera { user_defined
    location { function { x*0.8 } function { 0.1 } function { -4 } }
    direction { function { 0 } function { 0 } function { 1 } }
}
background { rgb 0 }
sphere { <0,2,-4>, 0.5 pigment { rgb 0 } finish { ambient 0 diffuse 0 }
    light { pigment { rgb 1 } brightness 4 nits samples 1 } no_image no_shadow
}
#macro Wax(Flesh)
    texture { pigment { rgb 1 } finish { ambient 0 diffuse 1 subsurface {
        method 2 translucency 1 pigment { rgb Flesh } volume_sampling 1
    } } }
#end
// the skin's tint is the texture's pigment, looked up in each brick's own frame
#macro Skin(Tint)
    texture { pigment { gradient y colour_map { [0 rgb Tint] [1 rgb Tint*0.8] } scale 2 translate -y }
        finish { ambient 0 diffuse 1 subsurface { method 2 translucency 1 colour rgb 0.8 thickness 1 } } }
#end
#declare Same = Wax(0.8)
#declare SameSkin = Skin(0.9)
#declare Brick = box { <-0.5,-1,-0.5>, <0.5,1,0.5> }
union {
    #switch (Layout)
    #case (0)
        object { Brick translate -x*0.5 }
        object { Brick translate x*0.5 }
        texture { Same }
    #break
    #case (1)
        object { Brick texture { Same } translate -x*0.5 }
        object { Brick texture { Same } translate x*0.5 }
    #break
    #case (2)
        object { Brick Wax(LeftFlesh) translate -x*0.5 }
        object { Brick texture { Same } translate x*0.5 }
    #break
    #case (3)
        object { Brick translate -x*0.5 }
        object { Brick translate x*0.5 }
        texture { SameSkin }
    #break
    #else
        object { Brick Skin(LeftSkin) translate -x*0.5 }
        object { Brick texture { SameSkin } translate x*0.5 }
    #end
    interior { ior 1.3 } subsurface { domain_cells 32 }
}
