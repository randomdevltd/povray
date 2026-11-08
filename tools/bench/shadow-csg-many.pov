// A field of hollow transparent CSG beads between six point lights and a floor; shadow rays crossing many CSG compounds dominate the trace.
#version 3.7;
#ifndef (Nx) #declare Nx = 24; #end
#ifndef (Nz) #declare Nz = 12; #end
#ifndef (Ny) #declare Ny = 11; #end
#ifndef (OpaqueEvery) #declare OpaqueEvery = 9; #end
global_settings { assumed_gamma 1.0 max_trace_level 8 }
camera { perspective location <0, 17, -38> look_at <0, 6.5, 3> angle 36 right x * 4 / 3 }

light_source { <-18, 30, -6>, rgb <1.0, 0.96, 0.9> }
light_source { < 18, 30, -6>, rgb <0.9, 0.95, 1.0> }
light_source { <-18, 26, 26>, rgb <0.95, 0.9, 1.0> }
light_source { < 18, 26, 26>, rgb <1.0, 1.0, 0.95> }
light_source { <-34, 10, 10>, rgb 0.8 }
light_source { < 34, 10, 10>, rgb 0.8 }

plane { y, 0 pigment { rgb <0.65, 0.63, 0.6> } finish { diffuse 0.75 } }

// Hollow bead: difference of two spheres, so every crossing tests CSG children and insides.
#declare Bead = difference
{
    sphere { <0, 0, 0>, 1 }
    sphere { <0, 0, 0>, 0.78 }
}

#declare I = 0;
#while (I < Nx)
    #declare J = 0;
    #while (J < Nz)
        #declare K = 0;
        #while (K < Ny)
            #declare N = I + J * Nx + K * Nx * Nz;
            object {
                Bead
                translate <(I - (Nx - 1) / 2) * 2.7, 1.3 + K * 2.0, (J - (Nz - 1) / 2) * 2.7>
                pigment { rgbf <mod(N, 5) / 12 + 0.65, 0.75, mod(N, 3) / 14 + 0.72, 0.7> }
                finish { ambient 0.02 diffuse 0.12 reflection 0.04 }
                interior { ior 1.45 }
            }
            #declare I2 = mod(N, OpaqueEvery);
            #if (I2 = 0)
                object {
                    Bead
                    translate <(I - (Nx - 1) / 2) * 2.7 + 1.35, 0.9 + K * 2.0, (J - (Nz - 1) / 2) * 2.7 + 1.35>
                    pigment { rgb <0.35, 0.32, 0.3> }
                    finish { diffuse 0.7 }
                }
            #end
            #declare K = K + 1;
        #end
        #declare J = J + 1;
    #end
    #declare I = I + 1;
#end
