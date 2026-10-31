// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A sunbeam through a window into dusty air in real units, as four copies of the room with the same dust coefficients seen from the same place: classic method 3 (top left), physical method 4 (top right), method 4 with scattering x 4 and extinction 0.25, which matches the classic look (bottom left), and absorption only, the surfaces and dimming without the glow (bottom right). +w960 +h640
#version 3.8;
// 1 m per unit; light colours are cd/pi (or lux/pi) times Exposure, so with Exposure 1 and +FH a white diffuse surface reads in cd/m2. Room 5 x 6 m and 3 m high, window 1.6 x 1.5 m. Dust: sigma_s = 0.03 per m, albedo 1, Henyey-Greenstein g = 0.6.
// Sun 100,000 lux, 35 degrees up; sky 4000 cd/m2; fill a 1500 cd ceiling light. Albedos: floor 0.3, walls 0.6, ceiling 0.7. The dust fills each room and reaches into its walls, so with media_attenuation on it dims the sun and the fill on their way.
#declare Exposure = 0.0004;
#declare SIG = 0.03;
#declare G = 0.6;
#declare Looks = array[4] {
  media { method 3 intervals 40 samples 3 jitter 0 aa_level 4 aa_threshold 0.002 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb 4*SIG eccentricity G extinction 0.25 } },
  media { method 4 absorption rgb SIG }
}
#declare Labels = array[4] { "Classic, method 3", "Physical, method 4", "Method 4, scattering x 4, extinction 0.25", "Absorption only" }
#declare Spacing = 8;
#declare SunC = <1, 0.95, 0.88> / (0.2126 + 0.7152*0.95 + 0.0722*0.88);
#declare Warm = <1, 0.85, 0.65> / 0.868;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb <0.6, 0.75, 1.0> / (0.2126*0.6 + 0.7152*0.75 + 0.0722) * 4000 * Exposure }

// each quarter of the image is the same perspective view into its own room, the rooms Spacing apart along z
#declare CamL = <4.6, 1.5, 5.6>;
#declare CamD = vnormalize(<1.0, 1.1, 2.6> - CamL);
#declare CamR = vnormalize(vcross(y, CamD));
#declare CamU = vcross(CamD, CamR);
#declare Aspect = image_width / image_height;
#declare Depth = 0.5 * Aspect / tan(radians(74 / 2));
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

#declare SD = vnormalize(<1, -0.75, 0.35>);
light_source { <0.0, 1.65, 2.8> - 60*SD, rgb SunC*100000/pi*Exposure parallel point_at <0.0, 1.65, 2.8> media_attenuation on media_interaction on }
#declare F = finish { ambient 0 diffuse 1 specular 0 }
#for (K, 0, 3)
  union {
    light_source { <2.5, 2.85, 3.0>, rgb Warm*1500/pi*Exposure fade_power 2 media_attenuation on media_interaction on }
    // walls only, with the window cut out: the floor and ceiling are the boxes below, so each surface has one albedo
    difference {
      box { <-0.25, -0.2, -0.2>, <5.2, 3.2, 6.2> } box { <0, -0.1, 0>, <5, 3.1, 6> }
      box { <-0.3, 0.9, 2.0>, <0.01, 2.4, 3.6> }
      pigment { rgb 0.6 } finish { F }
    }
    union {
      box { <-0.18, 0.9, 2.77>, <-0.12, 2.4, 2.83> }
      box { <-0.18, 1.62, 2.0>, <-0.12, 1.68, 3.6> }
      pigment { rgb 0.6 } finish { F }
    }
    box { <-0.1, -0.1, -0.1>, <5.1, 0, 6.1> pigment { rgb 0.3 } finish { F } }
    box { <-0.1, 3.0, -0.1>, <5.1, 3.1, 6.1> pigment { rgb 0.7 } finish { F } }
    box { <3.6, 0, 0.3>, <4.9, 0.8, 1.0> pigment { rgb 0.35 } finish { F } }
    // the label, half a metre in front of the camera at the top left of its quarter
    text { ttf "timrom.ttf" Labels[K] 0.001, 0 scale 0.035 no_shadow
      pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
      translate <-0.47 * Aspect, 0.40, Depth> * 0.5 / Depth
      matrix <CamR.x, CamR.y, CamR.z, CamU.x, CamU.y, CamU.z, CamD.x, CamD.y, CamD.z, CamL.x, CamL.y, CamL.z>
    }
    // the medium reaches into the walls, so every surface is inside it and light that reaches it is dimmed on the way
    box { <-0.1, -0.1, -0.1>, <5.1, 3.1, 6.1> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { media { Looks[K] } } }
    translate z * Spacing * (K + 1)
  }
#end
