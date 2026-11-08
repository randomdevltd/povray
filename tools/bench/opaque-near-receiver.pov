// Worst case for a shadow walk that stops at the first opaque root it finds rather than the nearest: small opaque beads just
// above the floor, under many clear shapes whose bounds reach down past them, lit by a jittered area light and a surface light
// so that neighbouring shadow rays keep missing the cached blocker. Iso=1 makes the clear shapes slow isosurfaces; R near 0.5
// packs the beads, and Hide=1 hides them from the camera, so that every shading point is on the floor and most of its shadow
// rays end on a bead.
#version 3.7;
#ifndef (N) #declare N = 16; #end
#ifndef (Layers) #declare Layers = 4; #end
#ifndef (Iso) #declare Iso = 0; #end
#ifndef (S) #declare S = 6; #end
#ifndef (R) #declare R = 0.3; #end
#ifndef (Hide) #declare Hide = 0; #end
global_settings { assumed_gamma 1.0 max_trace_level 8 }
camera { perspective location <0, 9, -9> look_at <0, 0, 0> angle 40 right x * 4 / 3 }

light_source { <-2, 12, -3>, rgb 0.7 area_light x * 3, z * 3, S, S jitter circular orient }
box { <-1.5, 9, 1>, <1.5, 9.1, 4> pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
    light { pigment { rgb 1 } brightness 4 nits samples int(S * S * 4 / 9) } }

plane { y, 0 pigment { rgb <0.7, 0.68, 0.65> } finish { ambient 0 diffuse 0.8 } }

#declare Clear = texture { pigment { rgbf <0.85, 0.95, 1, 0.9> } finish { ambient 0 diffuse 0.05 } }
#declare Span = 6;
#declare Gap = 2 * Span / N;

// Opaque beads nearest the receiver.
#for (I, 0, N - 1) #for (J, 0, N - 1)
    sphere { <(I + 0.5) * Gap - Span, 0.35, (J + 0.5) * Gap - Span>, R * Gap pigment { rgb <0.4, 0.3, 0.25> } finish { ambient 0 diffuse 0.8 } #if (Hide) no_image #end }
#end #end

// Clear shapes above them, each bounded from just above the floor so its box is entered before the beads.
#for (L, 0, Layers - 1) #for (I, 0, 3) #for (J, 0, 3)
    #local C = <(I + 0.5) * 3 - Span, 0, (J + 0.5) * 3 - Span>;
    #local Y = 1.5 + L * 1.2;
    #if (Iso)
        isosurface { function { abs(y - Y - 0.2 * sin(4 * x) * cos(4 * z)) - 0.15 } contained_by { box { C + <-1.4, 0.05, -1.4>, C + <1.4, Y + 0.6, 1.4> } }
            max_gradient 2.5 texture { Clear } }
    #else
        difference { box { C + <-1.4, 0.05, -1.4>, C + <1.4, Y + 0.15, 1.4> } box { C + <-1.5, 0, -1.5>, C + <1.5, Y - 0.15, 1.5> } texture { Clear } }
    #end
#end #end #end
