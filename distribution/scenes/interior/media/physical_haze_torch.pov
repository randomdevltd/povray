// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A torch beam in a smoky room in real units, as four copies of the room with the same smoke coefficients seen from the same place: classic method 3 (top left), physical method 4 (top right), method 4 with scattering x 4 and extinction 0.25, which matches the classic look (bottom left), and absorption only, the surfaces and dimming without the glow (bottom right). +w960 +h640
#version 3.8;
// 1 m per unit; light colours are cd/pi times Exposure, so with Exposure 1 and +FH a white diffuse surface reads in cd/m2. Room 4 x 5 m and 2.6 m high. Smoke: sigma_s = 0.2 per m, albedo 1, Henyey-Greenstein g = 0.5.
// Torch: LED, 3000 cd peak, spot 4/9 degrees, 3.3 m to the wall (about 230 lux in the spot); fill a 15 cd ceiling light (about 2.4 lux on the floor). Albedos: walls 0.5, floor 0.3, ceiling 0.6, cabinet 0.25. The smoke fills each room and reaches into its walls, so with media_attenuation on it dims both lights on their way.
#declare Exposure = 0.05;
#declare SIG = 0.2;
#declare G = 0.5;
#declare Looks = array[4] {
  media { method 3 intervals 40 samples 3 jitter 0 aa_level 4 aa_threshold 0.002 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb 4*SIG eccentricity G extinction 0.25 } },
  media { method 4 absorption rgb SIG }
}
#declare Labels = array[4] { "Classic, method 3", "Physical, method 4", "Method 4, scattering x 4, extinction 0.25", "Absorption only" }
#declare Spacing = 7;
#declare Cool = <0.92, 0.97, 1.08> / (0.2126*0.92 + 0.7152*0.97 + 0.0722*1.08);
#declare Warm = <1, 0.85, 0.65> / 0.868;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb 0 }

// each quarter of the image is the same perspective view into its own room, the rooms Spacing apart along z
#declare CamL = <3.0, 1.6, 0.3>;
#declare CamD = vnormalize(<0.4, 1.2, 4.2> - CamL);
#declare CamR = vnormalize(vcross(y, CamD));
#declare CamU = vcross(CamD, CamR);
#declare Aspect = image_width / image_height;
#declare Depth = 0.5 * Aspect / tan(radians(72 / 2));
#declare Lx = CamL.x; #declare Ly = CamL.y; #declare Lz = CamL.z;
#declare Dx = Depth * CamD.x; #declare Dy = Depth * CamD.y; #declare Dz = Depth * CamD.z;
#declare Rx = CamR.x; #declare Ry = CamR.y; #declare Rz = CamR.z;
#declare Ux = CamU.x; #declare Uy = CamU.y; #declare Uz = CamU.z;
#declare Room = function(x, y) { 1 + (x >= 0) + 2 * (y < 0) }
#declare Px = function(x) { (2 * x - select(x, -0.5, 0.5)) * Aspect }
#declare Py = function(y) { 2 * y - select(y, -0.5, 0.5) }
camera {
  user_defined
  location {
    function { Lx }
    function { Ly }
    function { Lz + Spacing * Room(x, y) }
  }
  direction {
    function { Dx + Px(x) * Rx + Py(y) * Ux }
    function { Dy + Px(x) * Ry + Py(y) * Uy }
    function { Dz + Px(x) * Rz + Py(y) * Uz }
  }
}

#declare TP = <2.8, 1.12, 4.3>;
#declare TA = <0, 1.4, 2.6>;
#declare TD = vnormalize(TA - TP);
#declare F = finish { ambient 0 diffuse 1 specular 0 }
#for (K, 0, 3)
  union {
    light_source { TP + 0.02*TD, rgb Cool*3000/pi*Exposure spotlight point_at TA radius 4 falloff 9 tightness 0
      fade_power 2 media_attenuation on media_interaction on }
    light_source { <2.0, 2.5, 2.5>, rgb Warm*15/pi*Exposure fade_power 2 media_attenuation on media_interaction on }
    // walls only: the floor and ceiling are the boxes below, so each surface has one albedo
    difference { box { <-0.2, -0.2, -0.2>, <4.2, 2.8, 5.2> } box { <0, -0.1, 0>, <4, 2.7, 5> } pigment { rgb 0.5 } finish { F } }
    box { <-0.1, -0.1, -0.1>, <4.1, 0, 5.1> pigment { rgb 0.3 } finish { F } }
    box { <-0.1, 2.6, -0.1>, <4.1, 2.7, 5.1> pigment { rgb 0.6 } finish { F } }
    box { <2.45, 0, 3.95>, <3.2, 1.0, 4.7> pigment { rgb 0.25 } finish { F } }
    box { <0, 0.75, 3.2>, <0.6, 0.79, 4.6> pigment { rgb 0.25 } finish { F } }
    box { <0.5, 0, 3.25>, <0.55, 0.75, 3.3> pigment { rgb 0.25 } finish { F } }
    box { <0.5, 0, 4.5>, <0.55, 0.75, 4.55> pigment { rgb 0.25 } finish { F } }
    union {
      cylinder { TP - 0.20*TD, TP - 0.01*TD, 0.022 }
      cylinder { TP - 0.04*TD, TP - 0.005*TD, 0.03 }
      pigment { rgb 0.05 } finish { F }
    }
    // the torch lens, 20000 cd/m2
    disc { TP - 0.004*TD, -TD, 0.026 no_shadow pigment { rgb Cool*Exposure } finish { ambient 0 diffuse 0 emission 20000 } }
    // the label, half a metre in front of the camera at the top left of its quarter
    text { ttf "timrom.ttf" Labels[K] 0.001, 0 scale 0.035 no_shadow
      pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
      translate <-0.47 * Aspect, 0.40, Depth> * 0.5 / Depth
      matrix <CamR.x, CamR.y, CamR.z, CamU.x, CamU.y, CamU.z, CamD.x, CamD.y, CamD.z, CamL.x, CamL.y, CamL.z>
    }
    // the medium reaches into the walls, so every surface and both lights are inside it
    box { <-0.1, -0.1, -0.1>, <4.1, 2.7, 5.1> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { media { Looks[K] } } }
    translate z * Spacing * (K + 1)
  }
#end
