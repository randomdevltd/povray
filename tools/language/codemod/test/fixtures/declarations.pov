// Declarations and scope
#version 3.7;
#declare Saved_Version = version;
#version 3.5;
#declare Radius = 1;
#local Count = 0;
#declare Radius = Radius * 2;
#declare fn = 3;
#declare Message = "two
lines";

#macro Scale_All(Factor, Out)
  #local Size = Factor * Radius;
  #local Size = Size + 1;
  #declare Total_Size = Size;
  #declare Factor = Factor + 1;
  #declare Out[0] = Size
  Size
#end

#declare Ball = sphere { 0, Scale_All(2, Slots) }
#undef Ball
#declare Ball = sphere { 0, 1 }
#version Saved_Version;
