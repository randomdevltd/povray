// A lattice of transparent reflecting spheres over a small scattering container; layered transparent crossings and media shadow rays dominate.
#version 3.7;
#ifndef (Nx) #declare Nx = 20; #end
#ifndef (Nz) #declare Nz = 16; #end
#ifndef (Ny) #declare Ny = 8; #end
global_settings { assumed_gamma 1.0 max_trace_level 8 }
camera { perspective location <-26, 14, -30> look_at <0, 6, 2> angle 37 right x * 4 / 3 }

light_source { <20, 26, -16>, rgb <1.1, 1.05, 1.0> media_attenuation on }
light_source { <-22, 14, 18>, rgb 0.85 media_attenuation on }
light_source { <4, 30, 24>, rgb 0.7 }

plane { y, 0 pigment { rgb <0.55, 0.55, 0.58> } finish { diffuse 0.8 } }

// One small media container in the lower middle of the lattice; modest samples keep it a shadow-ray cost, not the whole scene.
box {
    <-6, 0.4, -5>, <7, 7, 6>
    hollow
    pigment { rgbt 1 }
    interior {
        media {
            absorption 1.5
            scattering { 1, rgb <0.55, 0.65, 0.75> }
            method 1
            intervals 2
            samples 10
        }
    }
}

#declare I = 0;
#while (I < Nx)
    #declare J = 0;
    #while (J < Nz)
        #declare K = 0;
        #while (K < Ny)
            #declare N = I + J * Nx + K * Nx * Nz;
            sphere {
                <(I - (Nx - 1) / 2) * 1.9, 1.25 + K * 1.9, (J - (Nz - 1) / 2) * 1.9>, 0.62
                pigment { rgbf <0.85, 0.95, 1.0, 0.65> }
                #if (mod(N, 2) = 0)
                    finish { ambient 0.02 diffuse 0.1 reflection 0.3 }
                #else
                    finish { ambient 0.02 diffuse 0.1 reflection 0.2 }
                #end
                interior { ior 1.5 }
            }
            #declare K = K + 1;
        #end
        #declare J = J + 1;
    #end
    #declare I = I + 1;
#end
