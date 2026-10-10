#macro Place(P) object { Ball translate P } #end
//     ^ definition.function
#declare Ring = torus { 1, 0.1 }
//       ^ definition.constant
Place(y)
// <- reference.call
