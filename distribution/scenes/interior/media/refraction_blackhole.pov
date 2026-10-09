// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A black hole bends light through media refraction alone: the index of a Schwarzschild field in isotropic coordinates, n(r) = (1 + rs/4r)^3 / (1 - rs/4r), lenses the stars and the glowing disk into a photon ring. +w640 +h360; Declare=Angle=A sets refraction_angle, the bend allowed per step, Disk=0 drops the disk.
#version 4.0;
#ifndef (Disk) #declare Disk = 1; #end
#ifndef (Angle) #declare Angle = 0.25; #end

#declare RS = 1;          // Schwarzschild radius; the horizon sits at RS/4 in these coordinates
#declare Reach = 14;      // radius of the field's container
#declare Most = 60;       // the index above 1 at which the field is capped, just outside the horizon
#declare Index = function(r) { pow(1 + RS / (4 * r), 3) / (1 - RS / (4 * r)) }
#declare Edge = Index(Reach);

global_settings { assumed_gamma 1 refraction_angle Angle }
camera { location <0, 2.2, -32> look_at 0 angle 24 right x * image_width / image_height }

sky_sphere {
  pigment {
    granite scale 0.012
    color_map { [0 rgb 0] [0.62 rgb 0] [0.66 rgb 0.6] [0.7 rgb <0.9, 0.95, 1.4>] [1 rgb <1.8, 1.7, 1.4>] }
  }
  pigment {
    gradient y scale 0.1
    color_map { [0 rgbt <0.25, 0.35, 0.6, 0.6>] [0.04 rgbt 1] [0.96 rgbt 1] [1 rgbt <0.25, 0.35, 0.6, 0.6>] }
  }
}

sphere {
  0, Reach
  pigment { rgbt 1 }
  interior {
    media {
      method 3
      refraction Most
      density {
        function { select(sqrt(x * x + y * y + z * z) - RS * 0.26, 1,
                          min(1, max(0, (Index(sqrt(x * x + y * y + z * z)) - Edge) / Most))) }
      }
    }
  }
}

sphere { 0, RS * 0.26 pigment { rgb 0 } finish { diffuse 0 ambient 0 } }

#if (Disk)
  cylinder {
    -0.06 * y, 0.06 * y, 9
    pigment { rgbt 1 }
    interior {
      media {
        mix add
        method 3 intervals 1 samples 6
        emission rgb <2.2, 1.1, 0.45>
        density {
          function {
            select(sqrt(x * x + z * z) - 2.7, 0,
                   exp(-(sqrt(x * x + z * z) - 2.7) / 1.8) * (0.55 + 0.45 * sin(9 * sqrt(x * x + z * z) + atan2(z, x))))
          }
        }
      }
    }
    rotate <-4, 0, 9>
  }
#end
