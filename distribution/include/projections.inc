#version 4.0;

#declare ProjectionEquirectangular = 0;
#declare ProjectionMercator = 1;
#declare ProjectionMiller = 2;
#declare ProjectionStereographic = 3;
#declare ProjectionEqualArea = 4;
#declare ProjectionSingleOrigin = 0;
#declare ProjectionSphereOrigin = 1;
#declare ProjectionCylinderOrigin = 2;

#macro CylindricalProjectionCamera(Profile, Lower, Upper, Origin, Radius, Inward, CentreV, ForwardV, RightV, UpV)
  #local Bottom = radians(Lower);
  #local Top = radians(Upper);
  #switch (Profile)
    #case (ProjectionMercator)
      #local Y0 = asinh(tan(Bottom));
      #local Y1 = asinh(tan(Top));
      #local Latitude = function(v) { atan(sinh(Y0 + (v + 0.5) * (Y1 - Y0))) };
      #break
    #case (ProjectionMiller)
      #local Y0 = 1.25 * asinh(tan(0.8 * Bottom));
      #local Y1 = 1.25 * asinh(tan(0.8 * Top));
      #local Latitude = function(v) { atan(sinh((Y0 + (v + 0.5) * (Y1 - Y0)) / 1.25)) / 0.8 };
      #break
    #case (ProjectionStereographic)
      #local Y0 = 2 * tan(Bottom / 2);
      #local Y1 = 2 * tan(Top / 2);
      #local Latitude = function(v) { 2 * atan((Y0 + (v + 0.5) * (Y1 - Y0)) / 2) };
      #break
    #case (ProjectionEqualArea)
      #local Y0 = sin(Bottom);
      #local Y1 = sin(Top);
      #local Latitude = function(v) { asin(Y0 + (v + 0.5) * (Y1 - Y0)) };
      #break
    #else
      #local Latitude = function(v) { Bottom + (v + 0.5) * (Top - Bottom) };
  #end
  #local DX = function(u, v) { sin(2 * pi * u) * cos(Latitude(v)) };
  #local DY = function(u, v) { sin(Latitude(v)) };
  #local DZ = function(u, v) { cos(2 * pi * u) * cos(Latitude(v)) };
  #local CX = CentreV.x; #local CY = CentreV.y; #local CZ = CentreV.z;
  #local FX = ForwardV.x; #local FY = ForwardV.y; #local FZ = ForwardV.z;
  #local RX = RightV.x; #local RY = RightV.y; #local RZ = RightV.z;
  #local UX = UpV.x; #local UY = UpV.y; #local UZ = UpV.z;
  #local WX = function(u, v) { DX(u, v) * RX + DY(u, v) * UX + DZ(u, v) * FX };
  #local WY = function(u, v) { DX(u, v) * RY + DY(u, v) * UY + DZ(u, v) * FY };
  #local WZ = function(u, v) { DX(u, v) * RZ + DY(u, v) * UZ + DZ(u, v) * FZ };
  camera {
    user_defined
    #if (Origin = ProjectionSphereOrigin)
      location { function { CX + Radius * WX(x, y) } function { CY + Radius * WY(x, y) } function { CZ + Radius * WZ(x, y) } }
    #elseif (Origin = ProjectionCylinderOrigin)
      location {
        function { CX + Radius * (sin(2 * pi * x) * RX + cos(2 * pi * x) * FX) }
        function { CY + Radius * (sin(2 * pi * x) * RY + cos(2 * pi * x) * FY) }
        function { CZ + Radius * (sin(2 * pi * x) * RZ + cos(2 * pi * x) * FZ) }
      }
    #else
      location CentreV
    #end
    #if (Inward)
      direction { function { -WX(x, y) } function { -WY(x, y) } function { -WZ(x, y) } }
    #else
      direction { function { WX(x, y) } function { WY(x, y) } function { WZ(x, y) } }
    #end
  }
#end

#macro MercatorCamera(CentreV, ForwardV, RightV, UpV, Lower, Upper)
  CylindricalProjectionCamera(ProjectionMercator, Lower, Upper, ProjectionSingleOrigin, 0, 0, CentreV, ForwardV, RightV, UpV)
#end
