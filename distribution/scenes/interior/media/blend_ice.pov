// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Ice crystals with cloudy cores in golden haze, as a matrix. Columns: the left cores mix replace, so no haze leaks in; the right cores mix add, letting the haze in. Rows: in the top crystals air trapped in the core lowers the index, so the view through them ripples round the core; the bottom crystals keep an even index. The crystal on the right has no media at all. +w480 +h320
#version 4.0;
// Method 4 scattering is physical: colours are 4x the classic values with extinction 0.25, keeping the classic haze glow and its extinction.

global_settings { assumed_gamma 1 max_trace_level 16 }
camera { location <0, 2.45, -7.2> look_at <-0.1, 2.4, 1> angle 50 right x * image_width / image_height }
background { rgb <0.01, 0.015, 0.03> }
light_source { <-9, 9, 6> rgb <1.4, 1.5, 1.8> spotlight point_at <-1.05, 2.4, 1> radius 8 falloff 15 }
light_source { <6, 5, -8> rgb <0.5, 0.45, 0.4> media_interaction off }
plane { y, 0 pigment { rgb <0.15, 0.18, 0.24> } finish { reflection 0.15 } }
box { <-5, 0, -1.5>, <5, 5.4, 3.5> pigment { rgbt 1 } interior { media { scattering { 1, rgb <2, 1.32, 0.48> extinction 0.25 } absorption rgb <0.02, 0.05, 0.12> } } }

#macro Prism(Length, Width)
  intersection {
    #for (A, 0, 300, 60) plane { x, Width rotate y * A } #end
    plane { y, Length } plane { -y, Length }
  }
#end
#declare Ice = texture {
  pigment { rgbf <0.92, 0.97, 1, 0.97> }
  finish { specular 0.8 roughness 0.001 reflection { 0.02, 1 fresnel } conserve_energy }
}
#macro Core(Strength, Length, Width, Blend, Bend)
  interior {
    ior 1.31
    #local Cloud = density {
      spherical turbulence 0.5 octaves 5 lambda 2.5 scale <Width, Length, Width> * 0.85
      color_map { [0 rgb 0] [0.3 rgb 0] [0.6 rgb 0.6] [1 rgb 1] }
    }
    media { mix #if (Blend) replace #else add #end scattering { 1, rgb <2.4, 3.2, 4.8> * Strength extinction 0.25 } density { Cloud } }
    #if (Bend)
      #local CoreIndex = function { pigment { spherical turbulence 0.4 octaves 2 scale <Width, Length, Width> * 0.85 color_map { [0 rgb 0] [0.4 rgb 0.3] [1 rgb 1] } } };
      refraction {
        function { -0.1 * CoreIndex(x,y,z).red }
        detail auto
      }
    #end
  }
#end
#macro Crystal(Blend, Bend, X, Y)
  object { Prism(0.85, 0.5) texture { Ice } Core(9, 0.85, 0.5, Blend, Bend) rotate <10, 25, -12> translate <X, Y, 1> }
#end
Crystal(1, 1, -2.4, 3.55)
Crystal(0, 1, 0.3, 3.55)
Crystal(1, 0, -2.4, 1.2)
Crystal(0, 0, 0.3, 1.2)
object { Prism(1.6, 0.5) texture { Ice } interior { ior 1.31 } rotate <0, 30, 15> translate <2.7, 2.1, 1.4> }
