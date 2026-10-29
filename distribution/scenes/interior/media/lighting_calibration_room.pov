// Metre-scale calibration room: Source=1 sphere, 2 bulb envelope, 3 monitor, 4/5 strip.
// Radio=0/1, Samples=16, ShowProbes=1; linear HDR, common preview exposure -3 stops, AA off.
#version 3.8;
#ifndef (Source) #declare Source = 1; #end
#ifndef (Radio) #declare Radio = 0; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (ShowProbes) #declare ShowProbes = 1; #end

global_settings {
  assumed_gamma 1
  max_trace_level 12
  #if (Radio)
    radiosity { pretrace_start 0.08 pretrace_end 0.04 count 100 error_bound 0.5 recursion_limit 2 media off }
  #end
}
background { rgb 0 }
#default { finish { ambient 0 diffuse albedo 0.7 specular 0 } }

camera {
  location <1.65, 1.55, -1.25>
  look_at <-0.3, 1.05, 0.35>
  right x * image_width / image_height
  angle 72
}

#macro Matte(Reflectance)
  texture { pigment { rgb 1 } finish { ambient 0 diffuse albedo Reflectance specular 0 } }
#end
#macro TransparentEmitter()
  texture { pigment { rgbt 1 } finish { ambient 0 diffuse 0 specular 0 } }
  hollow no_radiosity
#end

box { <-2.01, -0.01, -1.51>, <2.01, 0, 1.51> Matte(0.2) }
box { <-2.01, 2.5, -1.51>, <2.01, 2.51, 1.51> Matte(0.8) }
box { <-2.01, 0, -1.51>, <-2, 2.5, 1.51> Matte(0.7) }
box { <2, 0, -1.51>, <2.01, 2.5, 1.51> Matte(0.7) }
box { <-2, 0, 1.5>, <2, 2.5, 1.51> Matte(0.7) }
box { <-2, 0, -1.51>, <2, 2.5, -1.5> Matte(0.7) }

box { <-0.7, 0.72, 0.65>, <0.7, 0.76, 1.25> Matte(0.35) }
#for (X, -0.6, 0.6, 1.2)
  #for (Z, 0.75, 1.15, 0.4)
    box { <X - 0.025, 0, Z - 0.025>, <X + 0.025, 0.72, Z + 0.025> Matte(0.25) }
  #end
#end

#if (ShowProbes)
  #for (X, -1.5, 1.5, 0.5)
    #for (Z, -1, 1, 0.5)
      box { <X - 0.035, 0.001, Z - 0.035>, <X + 0.035, 0.002, Z + 0.035> Matte(1) }
    #end
  #end
#end

#declare SphereL = 806 / (4 * pi * pi * 0.03 * 0.03);
#if (Source = 1)
  sphere {
    0, 0.03 TransparentEmitter()
    interior { media { method 4 emission rgb (SphereL / 0.06) }
      light { pigment { rgb 1 } brightness SphereL nits samples Samples attenuation 2, 1 } }
    translate <0, 1.7, 0>
  }
#end

#if (Source = 2)
  // Idealized A60 envelope plus base: 60 mm maximum width and 110 mm total height.
  // Surface area/luminance of the real product is not measured by these dimensions.
  lathe {
    linear_spline 9,
    <0, 0.025>, <0.013, 0.025>, <0.014, 0.04>, <0.023, 0.057>,
    <0.029, 0.072>, <0.03, 0.084>, <0.025, 0.099>, <0.015, 0.108>, <0, 0.11>
    TransparentEmitter()
    interior { media { method 4 emission rgb (SphereL / 0.06) }
      light { pigment { rgb 1 } brightness SphereL nits samples Samples attenuation 2, 1 } }
    translate <0, 1.645, 0>
  }
  cylinder { <0, 1.645, 0>, <0, 1.67, 0>, 0.013 Matte(0.25) }
#end

#if (Source = 3)
  #declare ScreenW = 0.59674;
  #declare ScreenH = 0.33566;
  #declare ScreenL = 350;
  box {
    <-ScreenW / 2, -ScreenH / 2, -0.0005>, <ScreenW / 2, ScreenH / 2, 0.0005>
    TransparentEmitter()
    interior { media { method 4 emission rgb (ScreenL / 0.001) }
      light { pigment { rgb 1 } brightness ScreenL nits samples Samples attenuation 2, 1 } }
    translate <0, 1.15, 0.62>
  }
  // Opaque backing blocks rear emission; no absorbing source medium is used.
  box { <-ScreenW / 2 - 0.002, 1.15 - ScreenH / 2 - 0.002, 0.6206>,
        <ScreenW / 2 + 0.002, 1.15 + ScreenH / 2 + 0.002, 0.65> Matte(0.05) }
#end

#if ((Source = 4) | (Source = 5))
  #declare StripFlux = 1700;
  #if (Source = 5) #declare StripFlux = 1140; #end
  #declare StripL = StripFlux / (pi * 2 * 0.015);
  box {
    <-1, -0.0005, -0.0075>, <1, 0.0005, 0.0075>
    TransparentEmitter()
    interior { media { method 4 emission rgb (StripL / 0.001) }
      light { pigment { rgb 1 } brightness StripL nits samples Samples attenuation 2, 1 } }
    translate <0, 2.35, 0>
  }
  box { <-1.002, 2.3506, -0.0095>, <1.002, 2.3561, 0.0095> Matte(0.05) }
#end

#if ((Source < 1) | (Source > 5)) #error "Source must be 1 through 5." #end
#debug "Calibration geometry and derived source assumptions: doc/volume-light-calibration.md\n"
