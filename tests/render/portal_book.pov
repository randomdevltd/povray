// A magic book: a picture on a page is a portal into a meadow vastly larger than the book.
// The meadow sits far away and lights only itself; rays reach it only through the portal, a quad just above the page.
#version 3.8;
#ifndef (Tilt) #declare Tilt = 40; #end           // how far the book is propped up toward the viewer, in degrees
#ifndef (Zoom) #declare Zoom = 25; #end           // how much larger the meadow is than the page
#ifndef (View) #declare View = 1; #end            // 1: looking down the page; 2: low and from the side, to see past the book
#ifndef (Rad) #declare Rad = 0; #end              // 1: radiosity on
global_settings
{
    assumed_gamma 1.0 max_trace_level 8
    #if (Rad) radiosity { pretrace_start 0.08 pretrace_end 0.02 count 150 error_bound 0.6 recursion_limit 2 } #end
}

#declare MeadowX = 10000;

#if (View = 1)
    camera { location <1.0, 4.6, -3.9> look_at <1.0, 0.1, 0.0> angle 36 }
#else
    camera { location <5.5, 1.2, -2.6> look_at <0.6, 0.3, 0.0> angle 38 }
#end

light_source { <-4, 7, -5> rgb 1.1 }

sky_sphere { pigment { gradient y color_map { [0 rgb <0.9, 0.85, 0.75>] [1 rgb <0.35, 0.45, 0.7>] } } }

// Table (finite, so rays that leave the meadow do not find it)
box { <-20, -1.11, -20>, <20, -0.91, 20> pigment { rgb <0.35, 0.22, 0.12> } finish { ambient 0.3 diffuse 0.8 } }

// Book
#declare Paper = pigment { rgb <0.93, 0.89, 0.78> };
union
{
    box { <-1.95, -0.1, -1.3>, <1.95, 0.0, 1.3> pigment { rgb <0.35, 0.05, 0.08> } }
    box { <-1.85, 0.0, -1.2>, <-0.04, 0.16, 1.2> pigment { Paper } finish { ambient 0.3 } }
    box { < 0.04, 0.0, -1.2>, < 1.85, 0.16, 1.2> pigment { Paper } finish { ambient 0.3 } }
    box { <-0.04, 0.0, -1.2>, < 0.04, 0.08, 1.2> pigment { rgb 0.15 } }

// The picture, 1.3 by 1.8, its top toward the back of the book; the meadow stays put however the book is tilted.
portal
{
    polygon { 5, <0.35, 0.1605, -0.9>, <0.35, 0.1605, 0.9>, <1.65, 0.1605, 0.9>, <1.65, 0.1605, -0.9>, <0.35, 0.1605, -0.9> }
    to { translate <-0.35, -0.161, 0.9> rotate <-90, 0, 0> translate <-0.65, -0.9, 0> scale Zoom translate <MeadowX, Zoom, 0> }
    far off
}

// Something coming out of the book: a vine rising from the picture, carrying a glowing lantern.
sphere_sweep
{
    cubic_spline 6
    <0.9, 0.0, -0.1>, 0.03
    <1.0, 0.15, 0.0>, 0.025
    <1.1, 0.5, 0.05>, 0.02
    <0.95, 0.8, 0.0>, 0.015
    <1.05, 1.1, -0.05>, 0.012
    <1.0, 1.3, 0.0>, 0.01
    pigment { rgb <0.15, 0.45, 0.12> }
}
sphere { <1.0, 1.4, 0.0>, 0.12
    pigment { rgb <1, 0.85, 0.4> }
    finish { emission 1.5 diffuse 0 }
}

rotate <-Tilt, 0, 0>
}

light_source { vrotate(<1.0, 1.4, 0.0>, <-Tilt, 0, 0>) rgb <1.0, 0.8, 0.4> * 0.5 fade_distance 1 fade_power 2 }

// The meadow
#declare Rnd = seed(11);
#macro Tree(Height, Shade)
    union
    {
        cylinder { <0, 0, 0>, <0, Height * 0.45, 0>, Height * 0.04 pigment { rgb <0.3, 0.2, 0.1> } }
        cone { <0, Height * 0.3, 0>, Height * 0.22, <0, Height, 0>, 0 pigment { rgb <0.05, 0.3 + 0.3 * Shade, 0.08> } }
    }
#end

light_group
{
    light_source { <MeadowX - 150, 400, -120> rgb <1.2, 1.15, 1.0> }
    box { <MeadowX - 300, -2, -50>, <MeadowX + 300, 0, 700> pigment { rgb <0.12, 0.35, 0.1> } }
    #declare I = 0;
    #while (I < 160)
        object
        {
            Tree(18 + 30 * rand(Rnd), rand(Rnd))
            translate <MeadowX + (rand(Rnd) - 0.5) * 500, 0, 25 + 600 * pow(rand(Rnd), 1.5)>
        }
        #declare I = I + 1;
    #end
    #declare I = 0;
    #while (I < 25)
        sphere
        {
            <MeadowX + (rand(Rnd) - 0.5) * 200, 5 + 40 * rand(Rnd), 30 + 200 * rand(Rnd)>, 1.2
            pigment { rgb <0.6 + 0.4 * rand(Rnd), 0.6 + 0.4 * rand(Rnd), 1> }
            finish { emission 2 diffuse 0 }
        }
        #declare I = I + 1;
    #end
    global_lights off
}
