// Shared parts of the portal and screen examples: a floor, a doorway, its frame and a lit world elsewhere to look into.
#include "tv.inc"

#declare Glow = finish { emission 1 diffuse 0 ambient 0 };

#macro Floor()
    box { <-60, -0.1, -60>, <60, 0, 60> pigment { checker rgb 0.78 rgb 0.62 } }
#end

#macro Sky()
    sky_sphere { pigment { gradient y color_map { [0 rgb <0.82, 0.86, 0.95>] [1 rgb <0.35, 0.5, 0.85>] } } }
#end

// A doorway W wide and H high, standing on y = 0 in the plane z = 0; its front faces -z.
#macro Door(W, H)
    polygon { 5, <-W / 2, 0, 0>, <-W / 2, H, 0>, <W / 2, H, 0>, <W / 2, 0, 0>, <-W / 2, 0, 0> }
#end

#macro Frame(W, H, Colour)
    union
    {
        box { <-W / 2 - 0.12, 0, -0.08>, <-W / 2, H + 0.12, 0.08> }
        box { <W / 2, 0, -0.08>, <W / 2 + 0.12, H + 0.12, 0.08> }
        box { <-W / 2 - 0.12, H, -0.08>, <W / 2 + 0.12, H + 0.12, 0.08> }
        pigment { rgb Colour }
    }
#end

// A flat monitor showing Picture in a W by H window, its lower edge on y = 0 and its face at z = 0.
#macro Panel(W, H, Picture, Bezel)
    union
    {
        box { <-W / 2 - 0.08, -0.08, 0>, <W / 2 + 0.08, H + 0.08, 0.1> pigment { rgb Bezel } }
        box { <0, 0, -0.01>, <W, H, 0> pigment { Picture scale <W, H, 1> } finish { Glow } translate -W / 2 * x }
    }
#end

// A sea at sunset three units below C, under its own sky and lit only by its own low sun.
#macro Elsewhere(C)
    light_group
    {
        light_source { C + <-150, 40, 600> rgb <1.3, 1.0, 0.75> }
        sphere { C, 900 inverse pigment { gradient y color_map { [0 rgb <1, 0.72, 0.4>] [0.12 rgb <0.92, 0.48, 0.5>] [0.4 rgb <0.3, 0.22, 0.55>] } scale 900 } finish { Glow } no_shadow }
        sphere { C + <-150, 40, 600>, 30 pigment { rgb <1, 0.95, 0.7> } finish { Glow } no_shadow }
        disc { C - 3 * y, y, 850 pigment { rgb <0.04, 0.2, 0.4> } finish { diffuse 0.7 specular 0.4 roughness 0.01 reflection 0.18 } normal { waves 0.08 scale 3 } }
        sphere { 0, 1 scale <30, 8, 20> translate C + <60, -4, 260> pigment { rgb <0.25, 0.2, 0.22> } }
        sphere { 0, 1 scale <14, 5, 10> translate C + <-40, -4, 140> pigment { rgb <0.3, 0.24, 0.24> } }
        global_lights off
    }
#end
