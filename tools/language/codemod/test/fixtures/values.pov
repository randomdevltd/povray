#include "colors.inc"
#default { finish { ambient 0.1 } }
#declare Grid = array[2][2] { { 1, 2 }, { 3, 4 } };
#declare Empty = array[4];
#declare Table = array[2][3];
#declare Props = dictionary { ["name"]: "ball", .size: 2, ["two words"]: 3 };
#declare On = ((Grid[0][1] = 2) & !(Props.size = 1));
camera { location <0, 1, -5> look_at 0 }
light_source { <10, 10, -10>, White }
plane { y, 0 pigment { color rgb <0.5, 0.5, 0.5> } }
#declare Padded = array[4] { 1, 2 };
#declare Key = "name";
#declare Mixed = dictionary { [Key]: 1, .step: 2, ["x"]: 3 };
#declare Steps = Mixed.step + Mixed["step"];
#undef Padded
#declare Spaced = dictionary { [Key ]: 1, [ "a" ]: 2 };
#declare Rows = array[2][3] { };
#declare Sized = array[Count + 1] { 1 };
#macro Forget()
  #undef Spaced
#end
#declare Kept = version;
#undef Kept
