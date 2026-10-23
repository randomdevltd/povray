// Two parallel lights through a method 4 medium, each with an optical-depth grid large enough to be built on several threads.
#version 3.8;
global_settings { assumed_gamma 1 }
camera { location <0, 2, -7> look_at 0 angle 45 }
light_source { <-6, 10, -8> rgb 1 parallel point_at 0 media_attenuation on }
light_source { <8, 5, -2> rgb <0.7, 0.6, 0.5> parallel point_at 0 media_attenuation on }
plane { y, -1.5 pigment { checker rgb 0.8, rgb 0.3 } }
sphere {
    0, 1.6 hollow pigment { rgbt 1 }
    interior { media {
        method 4 resolution 0.06
        absorption 0.4 scattering { 1, 0.8 }
        density { function { 0.5 + 0.5 * sin(x * 5) * cos(y * 4 + z * 3) } }
    } }
}
