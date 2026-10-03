// A television: a bezel with a recess, and a slightly convex glass screen showing Picture.
// The window is Width by Height with its lower edge at y = 0 and the bezel's front face at z = 0.
#macro TV(Width, Height, Picture)
    #local Half = Width / 2;
    #local Bulge = 0.04;
    #local Curve = (pow(Half, 2) + pow(Height / 2, 2)) / (2 * Bulge);
    union
    {
        difference
        {
            box { <-Half - 0.3, -0.1, 0.0>, <Half + 0.3, Height + 0.1, 0.8> }
            box { <-Half, 0.0, -0.01>, <Half, Height, 0.05> }
            pigment { rgb 0.08 }
        }
        intersection
        {
            box { <-Half - 0.01, -0.01, -0.01>, <Half + 0.01, Height + 0.01, 0.05> }
            sphere { <0, Height / 2, Curve>, Curve }
            texture
            {
                pigment { Picture scale <Width, Height, 1> translate <-Half, 0, 0> }
                finish { emission 1 diffuse 0 }
            }
            texture
            {
                pigment { rgbt 1 }
                finish { diffuse 0 specular 0.5 roughness 0.002 reflection 0.05 }
            }
        }
    }
#end
