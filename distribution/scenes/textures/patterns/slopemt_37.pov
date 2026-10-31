// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Slope pattern textures on a height field (render slopemt_dat_37.pov first): the vegetation's own bumps feed its slope, so soil and rock break through it in patches on the steeper ground. +w320 +h240
#version 4.0;
// The ambient fill this scene was painted with, which 3.8 and later default to 0.
#default { finish { ambient 0.1 } }

global_settings {
  assumed_gamma 1.0
}

camera {
   location <0,1,-40>
   right   x*image_width/image_height
   angle 50
   look_at <0,3,0>
}

// A bright blue sky.
sky_sphere {
   pigment {
      gradient y
      color_map {
         [0.0, color <1.0,1.0,1.0>]
         [0.2, color <0.6,0.75,1.0>*0.8]
         [1.0, color <0.5,0.6,0.8>*0.5]
      }
   }
}

light_source {<200,200,-200>, color rgb 1.2}

// Some simple water.
plane {
   y, 0
   pigment {color <0,0,0>}
   normal {bumps 0.03 scale 0.05}
   finish {reflection 0.7}
}

#declare Sand_Colour = <0.9,0.75,0.3>*0.9;
#declare Vege_Colour = <0.2,0.5,0.1>*0.5;
#declare Soil_Colour = <0.77,0.6,0.35>*0.7;
#declare Rock_Colour = <0.6,0.6,0.6>*0.5;

#declare Sand_Texture = texture { pigment {color Sand_Colour} }
#declare Rock_Texture = texture { pigment {color Rock_Colour} }
#declare Snow_Texture = texture { pigment {color rgb 1.0} }

// Vegetation on flat low ground, soil steeper and higher, rock steepest and highest, all in one layer.
#declare Vege2Rock_Texture =
texture {
   // Read after the normal below, so the vegetation's bumps tip this slope into soil and rock in patches.
   pigment {
      slope { -y*3, 0, 0.5 altitude y, 0.005, 0.400 }
      color_map {
         [0.45, color Vege_Colour]
         [0.55, color Soil_Colour]
         [0.65, color Soil_Colour]
         [0.65, color Rock_Colour]
      }
   }
   // Bumps where the vegetation grows, read from the smooth surface.
   normal {
      slope { -y*3, 0, 0.5 altitude y, 0.005, 0.400 }
      normal_map {
         [0.45, bumps 0.5 scale 0.005]
         [0.55, bumps 0]
      }
   }
}

// Snow on the flat high ground, rock on the steeper and lower ground.
#declare Rock2Snow_Texture =
texture {
   slope {
      -y*2, 0, 0.5
      altitude y, 1.000, 0.400
   }
   texture_map {
      [0.75, Snow_Texture]
      [0.75, Rock_Texture]
   }
}

// The gradient's bands match the altitude ranges of the slope textures above.
height_field {
   "slopemt_dat_37.jpg"
   scale <80,15,80>
   translate <-40,-5.5,-30>
   texture {
      gradient y
      scale 10
      texture_map {
         [0.000, Sand_Texture]
         [0.005, Sand_Texture]
         [0.005, Vege2Rock_Texture]
         [0.400, Vege2Rock_Texture]
         [0.400, Rock2Snow_Texture]
         [1.000, Rock2Snow_Texture]
      }
   }
}
