#declare A = 1;
#declare B = 0;
#declare Or_Then_And = (A | B & B);
#declare And_Then_Or = (A & B | B);
#declare Equal_Less = ((A = B) < 1);
#declare Chain = (A = B < 1);
#declare Scaled = 2 * #if (A) 3 #else 4 #end + 1;
#declare Picked = #if (A) /* first */ 1 #else 2 // second
#end;
sphere { 0, 1 scale #if (A) 2 #else 3 #end }
#macro Pick(P, Q) P #end
#declare Spaced_Args = Pick(1 <1, 2>);
#macro Shift(O, V) object { O translate V } #end
#macro Centred(O) object { O } #end
#macro Again(O) Centred(O) #end
#macro Half(V) V / 2 #end
object { Shift(Again(sphere { 0, 1 })
  -y - x) }
object { Shift(sphere { 0, 1 }, Half(y) - x) }
