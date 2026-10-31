// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A street lamp in night fog in real units, as four copies of the street with the same fog coefficients seen from the same place: classic method 3 (top left), physical method 4 (top right), method 4 with scattering x 4 and extinction 0.25, which matches the classic look (bottom left), and absorption only, the surfaces and dimming without the glow (bottom right). +w960 +h640
#version 3.8;
// 1 m per unit; light colours are cd/pi times Exposure, so with Exposure 1 and +FH a white diffuse surface reads in cd/m2. Fog: visibility 150 m, sigma_s = 3.9/V = 0.026 per m, albedo 1, Henyey-Greenstein g = 0.7.
// Lamp: 6 m high, about 3000 K, 900 cd down, cut-off spot 55/75 degrees. Albedos: road 0.12, kerb 0.25, building 0.35. The fog fills each street, so with media_attenuation on it dims the lamp on its way as well as the view.
#declare Exposure = 0.5;
#declare SIG = 0.026;
#declare G = 0.7;
#declare Looks = array[4] {
  media { method 3 intervals 40 samples 3 jitter 0 aa_level 4 aa_threshold 0.002 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb SIG eccentricity G } },
  media { method 4 samples 200 scattering { 5, rgb 4*SIG eccentricity G extinction 0.25 } },
  media { method 4 absorption rgb SIG }
}
#declare Labels = array[4] { "Classic, method 3", "Physical, method 4", "Method 4, scattering x 4, extinction 0.25", "Absorption only" }
#declare Spacing = 1000;
#declare Warm = <1, 0.85, 0.65> / 0.868;
#declare Icd = 900;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb 0 }

// each quarter of the image is the same perspective view along its own street, the streets Spacing apart along x
#declare CamL = <-1.0, 1.6, -6>;
#declare CamD = vnormalize(<2.0, 3.0, 25> - CamL);
#declare CamR = vnormalize(vcross(y, CamD));
#declare CamU = vcross(CamD, CamR);
#declare Aspect = image_width / image_height;
#declare Depth = 0.5 * Aspect / tan(radians(62 / 2));
#declare Lx = CamL.x; #declare Ly = CamL.y; #declare Lz = CamL.z;
#declare Dx = Depth * CamD.x; #declare Dy = Depth * CamD.y; #declare Dz = Depth * CamD.z;
#declare Rx = CamR.x; #declare Ry = CamR.y; #declare Rz = CamR.z;
#declare Ux = CamU.x; #declare Uy = CamU.y; #declare Uz = CamU.z;
#declare Street = function(x, y) { 1 + (x >= 0) + 2 * (y < 0) }
#declare Px = function(x) { (2 * x - select(x, -0.5, 0.5)) * Aspect }
#declare Py = function(y) { 2 * y - select(y, -0.5, 0.5) }
camera {
  user_defined
  location {
    function { Lx + Spacing * Street(x, y) }
    function { Ly }
    function { Lz }
  }
  direction {
    function { Dx + Px(x) * Rx + Py(y) * Ux }
    function { Dy + Px(x) * Ry + Py(y) * Uy }
    function { Dz + Px(x) * Rz + Py(y) * Uz }
  }
}

#declare Asph = texture { pigment { rgb 0.12 } finish { ambient 0 diffuse 1 specular 0 } }
#declare Stone = texture { pigment { rgb 0.25 } finish { ambient 0 diffuse 1 } }
#declare Paint = texture { pigment { rgb 0.5 } finish { ambient 0 diffuse 1 } }
#declare Metal = texture { pigment { rgb 0.08 } finish { ambient 0 diffuse 1 } }
#macro Post(Z)
  union {
    cylinder { <4.6, 0, Z>, <4.6, 6.1, Z>, 0.08 }
    cylinder { <4.6, 6.1, Z>, <2.9, 6.1, Z>, 0.04 }
    box { <2.0, 6.0, Z-0.3>, <3.4, 6.18, Z+0.3> }
    texture { Metal }
  }
#end
#for (K, 0, 3)
  union {
    light_source { <2.6, 5.9, 8>, rgb Warm*Icd/pi*Exposure spotlight point_at <2.6, 0, 8> radius 55 falloff 75 tightness 0
      fade_power 2 media_attenuation on media_interaction on }
    box { <-4, -1, -20>, <4, 0, 120> texture { Asph } }
    #for (I, -3, 23) box { <-0.07, 0, I*5>, <0.07, 0.002, I*5+2.5> texture { Paint } } #end
    box { <4, -1, -20>, <7.5, 0.12, 120> texture { Stone } }
    box { <-7, -1, -20>, <-4, 0.12, 120> texture { Stone } }
    box { <7.5, -1, -20>, <9, 11, 120> pigment { rgb 0.35 } finish { ambient 0 diffuse 1 } }
    #for (I, 0, 14) #for (J, 0, 2)
      box { <7.48, 1.0+J*3.3, -4+I*8>, <7.6, 2.6+J*3.3, -2+I*8> pigment { rgb 0.04 } finish { ambient 0 diffuse 1 } }
    #end #end
    Post(8) Post(38) Post(68) Post(98)
    // the lit lens of the lamp seen from below, 5000 cd/m2
    box { <2.1, 5.94, 7.8>, <3.1, 5.99, 8.2> no_shadow pigment { rgb Warm*Exposure } finish { ambient 0 diffuse 0 emission 5000 } }
    // the label, half a metre in front of the camera at the top left of its quarter
    text { ttf "timrom.ttf" Labels[K] 0.001, 0 scale 0.035 no_shadow
      pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
      translate <-0.47 * Aspect, 0.40, Depth> * 0.5 / Depth
      matrix <CamR.x, CamR.y, CamR.z, CamU.x, CamU.y, CamU.z, CamD.x, CamD.y, CamD.z, CamL.x, CamL.y, CamL.z>
    }
    // the fog reaches far past the lit street, so its glow and the dimming are as in open air
    box { <-40, -1.5, -30>, <40, 40, 160> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { media { Looks[K] } } }
    translate x * Spacing * (K + 1)
  }
#end
