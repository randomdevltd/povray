// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Gradient-index lenses: two flat glass discs, the left one with a dissolved concentration that raises its index toward the axis. Its curved photons focus sunlight to a spot; the plain disc beside it only casts a soft shadow. +w640 +h400
#version 4.0;
#ifndef (Photons) #declare Photons = 1; #end

global_settings {
  assumed_gamma 1
  #if (Photons) photons { spacing 0.004 } #end
}
camera { location <0, 5.2, -6.8> look_at <0, 1.3, 0> angle 44 right x * image_width / image_height }
light_source { <0, 40, 0> rgb 1.4 parallel point_at 0 photons { refraction on reflection off } }
light_source { <-6, 8, -9> rgb 0.25 shadowless }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.85, 0.88, 0.9>] [0.5 rgb <0.45, 0.6, 0.85>] } } }

plane { y, 0 pigment { checker rgb 0.85 rgb 0.6 scale 0.25 } finish { diffuse 0.8 } }

#declare Disc = cylinder { -0.25 * y, 0.25 * y, 1 };
#declare Glass = texture { pigment { rgbf <0.97, 0.99, 1, 1> } finish { diffuse 0 specular 0.4 roughness 0.01 reflection { 0, 1 fresnel } } };
#declare Stand = union {
  #for (A, 0, 2) cylinder { 0, y * 2.25, 0.03 translate x * 1.05 rotate y * (A * 120 + 30) } #end
  torus { 1.05, 0.03 translate y * 2.25 }
  pigment { rgb 0.15 } finish { specular 0.3 }
}

object {
  Disc
  texture { Glass }
  interior {
    ior 1.5
    media { method 3 refraction 0.4 density { function { max(0, 1 - (x * x + z * z)) } } }
  }
  photons { target refraction on reflection off collect off }
  translate <-1.2, 2.5, 0>
}
object { Stand translate x * -1.2 }

object {
  Disc
  texture { Glass }
  interior { ior 1.5 }
  photons { target refraction on reflection off collect off }
  translate <1.2, 2.5, 0>
}
object { Stand translate x * 1.2 }
