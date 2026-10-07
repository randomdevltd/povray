#version 4.0;

#ifndef (Count) #declare Count = 32; #end
#ifndef (Unique) #declare Unique = 1; #end

global_settings { assumed_gamma 1 }
camera { orthographic location <0, 0, -10> look_at 0 right 8*x up 8*y }
background { color rgb 0 }

#for (I, 0, Count - 1)
  #local A = 2*pi*I/Count;
  #local Name = concat("view-", str(I, 0, 0));
  sphere {
    <100 + 3*cos(A), 3*sin(A), 0>, 0.7
    pigment { color rgb <0.2 + 0.8*I/Count, 0.4, 1 - 0.8*I/Count> }
    finish { emission 1 diffuse 0 }
    tags { Name }
  }
#end

#for (I, 0, Count - 1)
  #local Column = mod(I, 8);
  #local Row = int(I/8);
  #if (Unique) #local Group = I; #else #local Group = mod(I, 4); #end
  #local Name = concat("view-", str(Group, 0, 0));
  #local View = camera {
    orthographic
    location <100 + 3*cos(2*pi*Group/Count), 3*sin(2*pi*Group/Count), -5>
    look_at <100 + 3*cos(2*pi*Group/Count), 3*sin(2*pi*Group/Count), 0>
    right 2*x
    up 2*y
    filter_tags { Name }
  }
  box {
    <-3.9 + Column, 3.9 - Row, 0>, <-3.1 + Column, 3.1 - Row, 0.01>
    pigment {
      screen { camera { View } }
      scale <0.8, 0.8, 1>
      translate <-3.9 + Column, 3.1 - Row, 0>
    }
    finish { emission 1 diffuse 0 }
  }
#end
