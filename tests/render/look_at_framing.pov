// look_at_framing: Case picks a framed camera; Reference=1 renders the camera it should equal.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1 }
#declare Glow = finish { emission 1 diffuse 0 }
#declare Subject = box { -1, 1 pigment { rgb <1, 0.2, 0.1> } finish { Glow } tags { "subject" } }
#declare Ground = plane { y, -1.5 pigment { checker rgb 0.1 rgb 0.4 } finish { Glow } }
#declare Back = box { <-1, -1, -6>, <1, 1, -4> pigment { rgb <0.2, 0.6, 1> } finish { Glow } tags { "behind" } }
#declare Wide = box { -30, 30 }
#declare Tiny = box { -1e-6, 1e-6 pigment { rgb <1, 0.6, 0.1> } finish { Glow } }
#declare Around = box { <-1, -1, -1>, <3, 1, 1> pigment { gradient x colour_map { [0 rgb <0.9, 0.9, 0.2>] [1 rgb <0.2, 0.9, 0.4>] } } finish { Glow } tags { "around" } hollow }
// The direction length that fits Subject's corners at 0.45 of the image, for the camera rolled and turned in cases 9 and 10.
#macro Turned(V) vrotate(vrotate(V, z * 30), y * 20) #end
#declare RolledDirection = 1e10;
#for (I, 0, 7)
  #declare Offset = <(mod(I, 2) ? 1 : -1), (mod(div(I, 2), 2) ? 1 : -1), (div(I, 4) ? 1 : -1)> - Turned(<0, 0, -10>);
  #declare C = vdot(Offset, Turned(z));
  #declare RolledDirection = min(RolledDirection, 0.45 * C / abs(vdot(Offset, Turned(x))), 0.45 * C / abs(vdot(Offset, Turned(y))));
#end
#switch (Case)
  #case (0) // object reference, perspective
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at <5, 3, 0> look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (1) // a tagged selection of objects placed before the camera
    object { Subject } object { Ground }
    camera { location <0, 0, -10> right x up y look_at <5, 3, 0> look_at tagged { "subject" } }
  #break
  #case (2) // an unbounded tagged object is skipped with a warning
    object { Subject } object { Ground }
    plane { z, -40 pigment { rgb 0.05 } finish { Glow } tags { "subject" } }
    camera { location <0, 0, -10> right x up y look_at tagged { "subject" } }
  #break
  #case (3) // a later vector look_at replaces framing
    #if (Reference) camera { location <0, 0, -10> right x up y look_at <0.5, 0.2, 0> }
    #else camera { location <0, 0, -10> right x up y look_at Subject look_at <0.5, 0.2, 0> } #end
    object { Subject } object { Ground }
  #break
  #case (4) // orthographic resizes right and up
    #if (Reference) camera { orthographic location <0, 0, -10> right x / 0.45 up y / 0.45 look_at 0 }
    #else camera { orthographic location <0, 0, -10> right x up y look_at <3, 0, 0> look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (5) // spherical: the target straddles the azimuth seam of the parsed view
    #if (Reference)
      #declare VA = degrees(atan2(1, 4)) / 0.45;
      camera { spherical location 0 angle 2 * VA VA look_at -z }
    #else camera { spherical location 0 look_at z look_at Back } #end
    object { Back } object { Ground }
  #break
  #case (6) // bounds around the camera: aimed at the centre, not zoomed, with a warning
    #if (Reference) camera { location 0 right x up y look_at <1, 0, 0> }
    #else camera { location 0 right x up y look_at Around } #end
    object { Around } object { Ground }
  #break
  #case (7) // error: an unbounded object
    camera { location <0, 0, -10> look_at Ground }
    object { Ground }
  #break
  #case (8) // error: a tagged selection with only unbounded objects
    plane { y, -1.5 pigment { rgb 0.4 } finish { Glow } tags { "ground" } }
    camera { location <0, 0, -10> look_at tagged { "ground" } }
  #break
  #case (9) // transforms after look_at roll and move the camera as they do a point look_at
    #if (Reference) camera { location <0, 0, -10> direction RolledDirection * z right x up y look_at 0 rotate z * 30 rotate y * 20 }
    #else camera { location <0, 0, -10> right x up y look_at Subject rotate z * 30 rotate y * 20 } #end
    object { Subject } object { Ground }
  #break
  #case (10) // a declared framed camera keeps framing beneath later transforms
    #if (Reference) camera { location <0, 0, -10> direction RolledDirection * z right x up y look_at 0 rotate z * 30 rotate y * 20 }
    #else
      #declare Framed = camera { location <0, 0, -10> right x up y look_at Subject }
      camera { Framed rotate z * 30 rotate y * 20 }
    #end
    object { Subject } object { Ground }
  #break
  #case (11) // an identifier frames the declared object, not a moved instance of it
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at Subject } #end
    object { Subject translate <2.5, 0, 0> } object { Ground }
  #break
  #case (12) // a target a few millionths of a unit across, orthographic
    #if (Reference) camera { orthographic location <0, 0, -10> right x * 1e-6 / 0.45 up y * 1e-6 / 0.45 look_at 0 }
    #else camera { orthographic location <0, 0, -10> right x up y look_at Tiny } #end
    object { Tiny }
  #break
  #case (13) // the same target through cylinder 3
    #if (Reference) camera { cylinder 3 location <0, 0, -10> right x up y * 1e-6 / 0.45 angle 90 * 1e-6 / 0.45 look_at 0 }
    #else camera { cylinder 3 location <0, 0, -10> right x up y angle 90 look_at Tiny } #end
    object { Tiny }
  #break
  #case (14) // focal blur without focal_point focuses where the equivalent camera would
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y aperture 0.6 blur_samples 4 look_at 0 }
    #else camera { location <0, 0, -10> right x up y aperture 0.6 blur_samples 4 look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (15) // a light_group's light is framed with its group, as a point
    light_group { light_source { <0, 3, 4> rgb 1 } object { Subject } tags { "subject" } } object { Ground }
    #declare Expected = box { <-1, -1, -1>, <1, 3, 4> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (16) // part of a union: only the selected child
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #end
    union { object { Subject } sphere { <3, 0, 0>, 0.5 pigment { rgb <0.3, 1, 0.3> } finish { Glow } tags { "other" } } }
    object { Ground }
    #if (!Reference) camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (17) // part of an intersection: the overlap of the selected children, not their union
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #end
    intersection {
      box { <-1, -1, -1>, <3, 1, 1> tags { "subject" } }
      box { <-3, -1, -1>, <1, 1, 1> tags { "subject" } }
      sphere { <0, 0, -1>, 0.4 inverse tags { "cut" } }
      pigment { rgb <1, 0.2, 0.1> } finish { Glow }
    }
    object { Ground }
    #if (!Reference) camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (18) // fisheye narrows angle until the box fits the image circle
    #if (Reference) camera { fisheye location <0, 0, -10> right x up y angle degrees(atan2(sqrt(2), 9)) / 0.45 look_at 0 }
    #else camera { fisheye location <0, 0, -10> right x up y angle 180 look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (19) // ultra_wide_angle with the box edges 76 degrees off axis
    #if (Reference) camera { ultra_wide_angle location <0, 0, -1.25> right x up y angle degrees(atan2(1, 0.25)) / 0.45 look_at 0 }
    #else camera { ultra_wide_angle location <0, 0, -1.25> right x up y angle 179 look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (20) // cylinder 1 narrows angle and up together
    #if (Reference) camera { cylinder 1 location <0, 0, -10> right x up y / 9 / 0.45 angle 90 / 9 / 0.45 look_at 0 }
    #else camera { cylinder 1 location <0, 0, -10> right x up y angle 90 look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (21) // panoramic and omnimax are aimed only, without a warning
    #if (Reference) camera { panoramic location <0, 0, -10> look_at 0 }
    #else camera { panoramic location <0, 0, -10> look_at <5, 3, 0> look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (22)
    #if (Reference) camera { omnimax location <0, 0, -10> look_at 0 }
    #else camera { omnimax location <0, 0, -10> look_at <5, 3, 0> look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (23) // the last camera wins, and only it frames its own target
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #else
      camera { location <0, 0, -10> right x up y look_at Back }
      camera { location <0, 0, -10> right x up y look_at Subject }
    #end
    object { Subject } object { Ground }
  #break
  #case (24) // a user_defined pinhole frames by narrowing its output window, as a perspective camera does
    camera { user_defined location { function { 0 } function { 0 } function { -10 } }
             direction { function { x } function { y } function { 1 } } look_at Subject }
    object { Subject } object { Ground }
  #break
  #case (25) // error: mesh cameras cannot frame
    camera { mesh_camera { 1 0 mesh { triangle { <0, 0, 0>, <1, 0, 0>, <0, 1, 0> } } } look_at Subject }
    object { Subject }
  #break
  #case (26) // an identifier target also frames under an old language version
    #version 3.1;
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at Subject } #end
    object { Subject } object { Ground }
  #break
  #case (27) // error: tagged is not a selector before 4.0
    #version 3.7;
    camera { location <0, 0, -10> look_at tagged { "subject" } }
  #break
  #case (28) // an early-built mesh inside a partly selected union refits the union's box
    #declare Ball = isosurface_mesh { function { x * x + y * y + z * z - 0.25 } contained_by { box { -0.6, 0.6 } }
                                      translate <0, 0, -1.5> pigment { rgb <0.3, 0.6, 1> } finish { Glow } }
    #declare Parts = union { object { Subject } object { Ball tags { "subject" } } }
    union { object { Parts } sphere { <3, 0, 0>, 0.5 pigment { rgb <0.3, 1, 0.3> } finish { Glow } tags { "other" } } }
    object { Ground }
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Parts }
    #else camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (29) // a frame multiplier below 1 leaves a wider border
    #if (Reference) camera { location <0, 0, -10> direction 3.6 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at Subject, 0.8 } #end
    object { Subject } object { Ground }
  #break
  #case (30) // above 1 crops into loose bounds
    #if (Reference) camera { location <0, 0, -10> direction 4.95 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at Subject, 1.1 } #end
    object { Subject } object { Ground }
  #break
  #case (31) // the multiplier on a tagged selection
    object { Subject } object { Ground }
    #if (Reference) camera { location <0, 0, -10> direction 3.6 * z right x up y look_at 0 }
    #else camera { location <0, 0, -10> right x up y look_at tagged { "subject" }, 0.8 } #end
  #break
  #case (32) // a later point look_at still replaces the framing and its multiplier
    #if (Reference) camera { location <0, 0, -10> right x up y look_at <0.5, 0.2, 0> }
    #else camera { location <0, 0, -10> right x up y look_at Subject, 0.8 look_at <0.5, 0.2, 0> } #end
    object { Subject } object { Ground }
  #break
  #case (33) // the multiplier on an orthographic camera
    #if (Reference) camera { orthographic location <0, 0, -10> right x / 0.55 up y / 0.55 look_at 0 }
    #else camera { orthographic location <0, 0, -10> right x up y look_at Subject, 1.1 } #end
    object { Subject } object { Ground }
  #break
  #case (34) // the multiplier on a spherical camera
    #if (Reference)
      #declare VA = degrees(atan2(1, 4)) / 0.4;
      camera { spherical location 0 angle 2 * VA VA look_at -z }
    #else camera { spherical location 0 look_at z look_at Back, 0.8 } #end
    object { Back } object { Ground }
  #break
  #case (35) // error: a multiplier that is not positive
    camera { location <0, 0, -10> look_at Subject, 0 }
    object { Subject }
  #break
  #case (36) // a user_defined target too large to fit leaves the parsed window, so the view is the unframed one
    #declare Pinhole = camera { user_defined location { function { 0 } function { 0 } function { -10 } }
                                direction { function { x } function { y } function { 1 } } }
    #if (Reference) camera { Pinhole } #else camera { Pinhole look_at Wide } #end
    object { Subject } object { Ground }
  #break
  #case (37) // objects written after the camera are not in its snapshot
    object { Subject } object { Ground }
    camera { location <0, 0, -10> right x up y look_at tagged { "subject" } }
    box { <-1, -1, -21>, <1, 1, -19> pigment { rgb 1 } finish { Glow } tags { "subject" } }
  #break
  #case (38) // error: a selection with nothing placed yet
    camera { location <0, 0, -10> look_at tagged { "subject" } }
    object { Subject }
  #break
  #case (39) // the tag expression grammar: wildcards, !, &, |, any and none
    object { Subject } object { Ground }
    sphere { <4, 0, 0>, 0.5 pigment { rgb <0.3, 1, 0.3> } finish { Glow } tags { "subordinate", "draft" } }
    sphere { <-4, 0, 0>, 0.5 pigment { rgb <0.3, 0.3, 1> } finish { Glow } }
    sphere { <0, 4, 0>, 0.5 pigment { rgb <1, 1, 0.3> } finish { Glow } tags { "other" } }
    #declare Expected = union { object { Subject } sphere { <-4, 0, 0>, 0.5 } };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at tagged { ("sub*" & !"draft" & any) | (none & !"other") } } #end
  #break
  #case (40) // a target the camera's own filter hides is still framed
    object { Subject } object { Ground }
    sphere { <0.8, 0.5, -1.5>, 0.4 pigment { rgb <0.3, 1, 0.3> } finish { Glow } }
    #if (Reference) camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 filter_tags { !"subject" } }
    #else camera { location <0, 0, -10> right x up y look_at <5, 3, 0> look_at tagged { "subject" } filter_tags { !"subject" } } #end
  #break
  #case (41) // a placed selection is a moved copy; the originals stay
    object { Subject } object { Ground }
    #if (Reference) object { Subject translate <3, 0.5, 0> }
    #else object { tagged { "subject" } translate <3, 0.5, 0> } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (42) // objects a macro places in a loop
    #macro Place(I) box { <I * 2, 0, 0>, <I * 2 + 1, 1, 1> pigment { rgb <1, 0.2, 0.1> } finish { Glow } tags { "subject" } } #end
    #for (I, -1, 1) Place(I) #end
    object { Ground }
    #declare Expected = box { <-2, 0, 0>, <3, 1, 1> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (43) // the command-line filter removes objects before any selection sees them
    object { Subject } object { Ground }
    box { <5, -1, -1>, <7, 1, 1> pigment { rgb 1 } finish { Glow } tags { "subject", "far" } }
    camera { location <0, 0, -10> right x up y look_at tagged { "subject" } }
  #break
  #case (44) // an empty selection placed adds nothing, with a warning
    object { Subject } object { Ground }
    object { tagged { "nothing" } }
    camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
  #break
  #case (45) // a declared selection frames the objects where they were placed
    object { Subject translate <2.5, 0, 0> } object { Ground }
    #declare Selected = tagged { "subject" };
    #declare Expected = object { Subject translate <2.5, 0, 0> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at Selected } #end
  #break
  #case (46) // a tagged light is part of a selection, framed as its point
    light_source { <0, 3, 4> rgb 1 tags { "subject" } }
    object { Subject } object { Ground }
    #declare Expected = box { <-1, -1, -1>, <1, 3, 4> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at tagged { "subject" } } #end
  #break
  #case (47) // geometry copied out of a light group is lit by the global lights
    light_group { light_source { <0, 5, -5> rgb 1 } sphere { 0, 1 pigment { rgb 1 } tags { "lamp" } } }
    light_source { <0, 0, -20> rgb 1 }
    #if (Reference) sphere { <3, 0, 0>, 1 pigment { rgb 1 } }
    #else object { tagged { "lamp" } translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (48) // a copy keeps the tags of what it copies, so filters treat it the same
    object { Ground }
    #if (!Reference)
      object { Subject }
      object { tagged { "subject" } translate <3, 0.5, 0> }
    #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> filter_tags { !"subject" } }
  #break
  #case (49) // a member of a light group with no lights is lit by the lights where its copy is placed
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 1 } }
    light_source { <0, 0, -20> rgb 1 }
    light_group { sphere { 0, 1 pigment { rgb 1 } tags { "dark" } } }
    #if (Reference) sphere { <3, 0, 0>, 1 pigment { rgb 1 } }
    #else object { tagged { "dark" } translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (50) // a selected light is copied: the placed, moved copy lights a second spot
    plane { y, -1 pigment { rgb 1 } }
    light_source { <0, 3, 0> rgb 1 spotlight point_at <0, -1, 0> radius 10 falloff 15 tags { "lamp" } }
    #if (Reference) light_source { <3, 3, 0> rgb 1 spotlight point_at <3, -1, 0> radius 10 falloff 15 }
    #else object { tagged { "lamp" } translate x * 3 } #end
    camera { location <1.5, 4, -8> right x up y look_at <1.5, -1, 0> }
  #break
  #case (51) // a selection inside a CSG
    object { Subject } object { Ground }
    #if (Reference) union { object { Subject } sphere { <3, 0, 0>, 0.5 pigment { rgb <0.3, 1, 0.3> } finish { Glow } } translate y * 2 }
    #else union { tagged { "subject" } sphere { <3, 0, 0>, 0.5 pigment { rgb <0.3, 1, 0.3> } finish { Glow } } translate y * 2 } #end
    camera { location <1.5, 1, -12> right x up y look_at <1.5, 1, 0> }
  #break
  #case (52) // a selection passed to a macro
    #macro Shift(Thing) object { Thing translate <3, 0.5, 0> } #end
    object { Subject } object { Ground }
    #if (Reference) Shift(Subject) #else Shift(tagged { "subject" }) #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (53) // a selection of a placed selection
    object { Subject } object { Ground }
    #if (Reference)
      object { Subject translate x * 3 }
      object { Subject translate <3, 2.5, 0> }
    #else
      object { tagged { "subject" } translate x * 3 tags { "copy" } }
      object { tagged { "copy" } translate y * 2.5 }
    #end
    camera { location <1.5, 1, -14> right x up y look_at <1.5, 1, 0> }
  #break
  #case (54) // copies of a split union keep the union's tags
    #declare Pair = union { box { <-1, -1, -1>, <0, 1, 1> } sphere { <1, 0, 0>, 0.5 } pigment { rgb <1, 0.2, 0.1> } finish { Glow } }
    #if (Reference)
      object { Pair } object { Pair translate y * 2.5 }
      camera { location <0, 1, -12> right x up y look_at <0, 1, 0> }
    #else
      object { Pair tags { "pair" } } object { Ground }
      object { tagged { "pair" } translate y * 2.5 }
      camera { location <0, 1, -12> right x up y look_at <0, 1, 0> filter_tags { "pair" } }
    #end
  #break
  #case (60) // a member copied into another light_group is lit by that group's lights, not the global ones
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 1 } }
    light_source { <0, 0, -20> rgb 0.5 }
    light_group { light_source { <0, 5, -5> rgb <1, 0.3, 0.3> } sphere { 0, 1 pigment { rgb 1 } tags { "lamp" } } }
    #if (Reference) light_group { light_source { <3, -5, -5> rgb <0.3, 0.3, 1> } sphere { <3, 0, 0>, 1 pigment { rgb 1 } } }
    #else light_group { light_source { <3, -5, -5> rgb <0.3, 0.3, 1> } object { tagged { "lamp" } translate x * 3 } } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (61) // a light_group copied whole brings its lights, which light the copy as they light the original
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 1 } }
    light_source { <0, 0, -20> rgb 0.3 }
    #declare Lamp = light_group {
      light_source { <0, 3, -3> rgb <1, 0.6, 0.3> }
      sphere { 0, 1 pigment { rgb 1 } } box { <-1.5, -1.5, 1>, <1.5, 1.5, 1.2> pigment { rgb 1 } }
      tags { "lamp" }
    }
    object { Lamp }
    #if (Reference) object { Lamp rotate z * 30 translate x * 3 }
    #else object { tagged { "lamp" } rotate z * 30 translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (62) // a group selected whole, with a child that matches on its own as well, is copied once
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 1 } }
    light_source { <0, 0, -20> rgb 0.3 }
    #declare Lamp = light_group { light_source { <0, 3, -3> rgb <1, 0.6, 0.3> } sphere { 0, 1 pigment { rgb 1 } tags { "bulb" } } tags { "lamp" } }
    object { Lamp }
    #if (Reference) object { Lamp translate x * 3 }
    #else object { tagged { "lamp" | "bulb" } translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (63) // a group copied whole keeps its global_lights setting
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 1 } }
    light_source { <0, -3, -20> rgb <0.3, 0.3, 1> }
    #declare Lamp = light_group { light_source { <0, 3, -3> rgb <1, 0.6, 0.3> } sphere { 0, 1 pigment { rgb 1 } } global_lights on tags { "lamp" } }
    object { Lamp }
    #if (Reference) object { Lamp translate x * 3 }
    #else object { tagged { "lamp" } translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (64) // a selected portal is copied, and the moved copy leads where the original does
    #declare Away = transform { translate 1000 * x }
    box { <-3, -3, 2>, <3, 3, 2.1> pigment { rgb 0.3 } finish { Glow } transform Away }
    sphere { <0.4, 0.3, 1>, 0.5 pigment { rgb <0.2, 1, 0.3> } finish { Glow } transform Away }
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 0.6 } finish { Glow } }
    portal { disc { 0, -z, 1 } to { transform Away } far off tags { "door" } }
    #if (Reference) portal { disc { 0, -z, 1 } to { transform Away } far off rotate z * 40 translate x * 3 }
    #else object { tagged { "door" } rotate z * 40 translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (65) // a copied portal makes its own far mouth when placed, as a second portal written there does
    #declare Away = transform { translate 1000 * x }
    sphere { <0.4, 0.3, 1>, 0.5 pigment { rgb <0.2, 1, 0.3> } finish { Glow } transform Away }
    box { <-6, -3, 2>, <6, 3, 2.1> pigment { rgb 0.6 } finish { Glow } }
    portal { disc { 0, -z, 1 } to { transform Away } tags { "door" } }
    #if (Reference) portal { disc { 0, -z, 1 } to { transform Away } translate x * 3 }
    #else object { tagged { "door" } translate x * 3 } #end
    camera { location <1.5, 0, -10> right x up y look_at <1.5, 0, 0> }
  #break
  #case (66) // look_at a point light aims at it and keeps the field, silently
    object { Subject } object { Ground }
    #declare Lamp = light_source { <3, 1, 0> rgb 1 }
    #if (Reference) camera { location <0, 0, -10> right x up y look_at <3, 1, 0> }
    #else camera { location <0, 0, -10> right x up y look_at Lamp } #end
  #break
  #case (67) // look_at an area light frames the area
    object { Subject } object { Ground }
    #declare Lamp = light_source { 0 rgb 1 area_light 2 * x, 2 * y, 2, 2 }
    #declare Expected = box { <-1, -1, 0>, <1, 1, 0> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at Lamp } #end
  #break
  #case (68) // a transformed area light frames the area where the transforms put it
    object { Subject } object { Ground }
    #declare Lamp = light_source { 0 rgb 1 area_light 2 * x, 2 * y, 2, 2 rotate x * 90 translate <1, 0.5, 0> }
    #declare Expected = box { <0, 0.5, -1>, <2, 0.5, 1> };
    #if (Reference) camera { location <0, 0, -10> right x up y look_at Expected }
    #else camera { location <0, 0, -10> right x up y look_at Lamp } #end
  #break
  #case (69) // a user_defined camera keeps its whole window on a point light it cannot turn to
    #declare Pinhole = camera { user_defined location { function { 0 } function { 0 } function { -10 } }
                                direction { function { x } function { y } function { 1 } } }
    #declare Lamp = light_source { <3, 1, 0> rgb 1 }
    #if (Reference) camera { Pinhole } #else camera { Pinhole look_at Lamp } #end
    object { Subject } object { Ground }
  #break
  #case (55) // error: a user_defined camera none of whose rays reaches the target
    #declare Behind = box { <-1, -1, -21>, <1, 1, -19> }
    camera { user_defined location { function { 0 } function { 0 } function { -10 } }
             direction { function { x } function { y } function { 1 } } look_at Behind }
  #break
  #case (56) // error: a camera whose right vector lies along the line to the target
    object { Subject }
    camera { location <0, 10, 0> direction x right y up z look_at Subject }
  #break
  #case (57) // a screen's camera frames its target, once when screens share it
    object { Subject } object { Ground }
    #if (Reference) #declare Monitor = camera { location <0, 0, -10> direction 4.05 * z right x up y look_at 0 }
    #else #declare Monitor = camera { location <0, 0, -10> right x up y look_at Subject } #end
    #macro Panel(X) box { <-1, -1, 0>, <1, 1, 0.01> pigment { screen { camera { Monitor } } } finish { Glow } translate <X, 0, -20> } #end
    Panel(-1.1) Panel(1.1)
    camera { location <0, 0, -25> right x up y look_at <0, 0, -20> }
  #break
  #case (58) // a shared screen camera whose target does not fit warns once
    object { Around } object { Ground }
    #declare Monitor = camera { location 0 right x up y look_at Around }
    #macro Panel(X) box { <-1, -1, 0>, <1, 1, 0.01> pigment { screen { camera { Monitor } } } finish { Glow } translate <X, 0, -20> } #end
    Panel(-1.1) Panel(1.1)
    camera { location <0, 0, -25> right x up y look_at <0, 0, -20> }
  #break
  #case (59) // error: an intersection bounded only by an inverted operand has no box to frame
    #declare Cut = intersection { plane { y, 0 } sphere { 0, 1 inverse } }
    camera { location <0, 0, -10> look_at Cut }
  #break
#end
