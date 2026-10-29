// A method 4 medium whose density tests an object (a potential pattern), so its grid is filled after the parse;
// media_tested_density_threads.sh requires the same image on one thread and on several.
#version 3.8;
global_settings { assumed_gamma 1 }
camera { location <0, 2, -7> look_at 0 angle 45 }
light_source { <-6, 10, -8> rgb 1 }
plane { y, -1.5 pigment { checker rgb 0.8, rgb 0.3 } }
sphere {
    0, 1.6 hollow pigment { rgbt 1 }
    interior { media {
        method 4 resolution 0.06
        absorption 0.6 scattering { 1, 0.4 } emission 0.05
        density { function { 0.5 + 0.5 * sin(x * 5) * cos(y * 4 + z * 3) } }
        density { potential { blob { threshold 0.5 sphere { <-0.5, 0, 0>, 1.2, 1 } sphere { <0.6, 0.3, 0>, 1.0, 1 } } }
                  color_map { [0 rgb 0.2] [1 rgb 1] } }
    } }
}
