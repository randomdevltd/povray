#version 4.0;
global_settings { assumed_gamma 1 }

#declare M = skein_mesh { expressions { extrude { radius 0.5 } extrude { axis x radius 2 } } closed uv max_angle 10 }
#declare First = union { object { M } object { M translate 5*x } }
#declare M = skein_mesh { expressions { extrude { radius 0.5 } extrude { axis x radius 2 } } closed uv max_angle 20 }
#declare Second = difference { object { M } sphere { <0, 0, 2>, 0.2 } }
#declare Extent = max_extent(Second);
#if (Extent.x < 0.49 | Extent.x > 0.51 | Extent.y < 2.49 | Extent.y > 2.51)
  #error "deferred mesh extent did not resolve the redeclared mesh"
#end

#declare SphereDistance = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 }
#declare SphereMesh = isosurface_mesh { function { SphereDistance(x, y, z) } contained_by { sphere { 0, 1.2 } } }
#declare SphereExtent = max_extent(SphereMesh);
#declare BoxDistance = function(x, y, z) { max(abs(x), abs(y), abs(z)) - 1 }
#declare BoxMesh = isosurface_mesh { function { BoxDistance(x, y, z) } contained_by { box { -1.2, 1.2 } } }
#declare BoxExtent = max_extent(BoxMesh);
#if (SphereExtent.x < 0.9 | SphereExtent.x > 1.1 | BoxExtent.x < 0.9 | BoxExtent.x > 1.1)
  #error "deferred isosurface meshes did not use their captured functions"
#end

camera { location <3, 6, -10> look_at <3, 0, 0> }
light_source { <-4, 8, -6> rgb 1 }
object { First pigment { rgb <0.7, 0.4, 0.2> } }
object { Second pigment { rgb <0.2, 0.5, 0.8> } }
object { SphereMesh translate -3*x pigment { rgb <0.3, 0.7, 0.4> } }
object { BoxMesh translate 3*x pigment { rgb <0.7, 0.3, 0.5> } }
