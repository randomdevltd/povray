# Portals and screens: the manual chapter and its images

The user documentation is chapter 3.10 of the HTML manual, `doc/html/r3_10.html`, linked from the
contents (`r3_0.html`), the keyword list (`r3_3.html`), the camera and radiosity sections
(`r3_4.html` 3.4.2.6 and 3.4.3.3.4.7), the objects (`r3_5.html` 3.5.1.6) and the special patterns
(`r3_6.html` 3.6.2.4.5). This file is for whoever changes the feature or the chapter.

## Where the behaviour is defined

| Topic | Source |
| --- | --- |
| `screen` syntax and its errors | `source/parser/parser_materials.cpp`, case `SCREEN_TOKEN` |
| `radiosity_size`, `no_radiosity` on a camera | `source/parser/parser.cpp`, `Parse_Camera_Radiosity_Size` and the camera parser |
| `portal` syntax, defaults and errors | `source/parser/parser.cpp`, `Parse_Portal` |
| Placement rules and warnings | `parser.cpp`: `Parse_CSG`, `Post_Process`, `Link_To_Frame`, `Check_Portal_Cameras`, `Make_Portal_Lights` |
| The window, shadows and photons on a screen | `source/core/material/pattern.cpp`, `ScreenPattern::Evaluate` |
| Screen views and their nesting | `source/core/render/tracepixel.cpp`, `Trace::TraceScreen` |
| Mouths, sides, the far mouth, chords | `source/core/shape/portal.h` and `portal.cpp` |
| Portal views, nesting, `exit` | `source/core/render/trace.cpp`, `TracePortal` and `TracePortalView` |
| Lights through portals, diverted light | `trace.cpp`, `ComputePortalDiffuseLight`, `TracePortalLightShadowRay`, `DivertPortalLight` |
| Screen camera pretraces | `source/backend/scene/view.cpp`, `ScreenPretraces` |
| `NESTED_VIEW_DEPTH_LIMIT` | `source/core/configcore.h` |

A change to any of these should be checked against the chapter, in particular its option tables,
the errors and warnings in 3.10.6, and the known limits.

## The rules that are easy to get wrong

- `to { }` maps the body as written; transforms on the portal move only the body, never the image.
- A surface's front is the side its normal points to; its far mouth faces where views come out.
  A closed solid's front is its outside, for both mouths.
- Straight light is diverted only when the partner mouth exists, has its lights on, and has the
  matching side open: the same side name between a surface and its image, the opposite between two
  closed solids. `no_lights` on a mouth stops light coming out of that mouth.
- Light images are made from real lights only, so a light path crosses at most one portal.
- Screens share a camera when their views match by content; `radiosity_size` is not compared, and
  a shared pretrace pass takes the largest size asked for.

## The images

Every image in the chapter is rendered by one script from a named scene:

    sh doc/portals/render.sh <povray> <source dir> <output dir> [image...]

With no image names it renders all of them; otherwise only those named (for example
`portal_r_exit portal_w07_jar`). It writes `<image>.png` at 320 by 240 with `+A0.1 +AM2 +R3 -J
+SS1`, and exits non-zero naming any scene that failed. Copy the results to `doc/html/images/`.

Names follow the skein chapter: `portal_o_*` for the overview, `portal_r_*` for reference
examples of one setting each, and `portal_wNN_*` for the worked use cases. Scenes are in
`doc/portals` (shared parts in `portals.inc`), except four that reuse the render fixtures in
`tests/render`: `portal_w03_feedback` (`screen_feedback.pov`, `Declare=Depth=12`),
`portal_w04_book` (`portal_book.pov`), `portal_w07_jar` (`portal_belljar.pov`) and the `TV`
macro from `tv.inc`. `portal_w08_candle` is `portal_w05_doorways.pov` with `Declare=Night=1`, and
`portal_r_lights_off` is `portal_r_lights.pov` with `Declare=NoLights=1`.

The images in the chapter were rendered by the build of commit 1d5be977. The whole set takes a few
seconds. Picture noise from area lights and radiosity differs slightly between builds even with a
fixed seed, so regenerated images can differ in their last bits without any change in behaviour;
compare by eye, or regenerate the whole set on one build.
