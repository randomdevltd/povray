#declare Ball = sphere { <0, 1, 0>, 0.5 pigment { rgb x } }
// <- keyword.directive
//       ^ variable
//              ^ keyword
//                                  ^ number
//                                      ^ keyword
//                                                ^ keyword
//                                                    ^ constant.builtin
#macro Place(P) object { Ball translate P } #end
//     ^ function
//           ^ variable.parameter
Place(vnormalize(<1, 1, 0>))
// <- function.call
//    ^ function.builtin
