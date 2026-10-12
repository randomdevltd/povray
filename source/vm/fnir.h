//******************************************************************************
///
/// @file vm/fnir.h
///
/// The register IR for render-time functions: the SSA form fncode is lowered
/// to at compile time, executed by the IR interpreter and, later, by wider or
/// native tiers reading the same definitions.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License
/// as published by the Free Software Foundation, either version 3 of
/// the License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
/// the GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

#ifndef POVRAY_VM_FNIR_H
#define POVRAY_VM_FNIR_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "vm/configvm.h"
#include "vm/fnpovfpu_fwd.h"

// C++ standard header files
#include <cstdint>
#include <memory>
#include <vector>

// POV-Ray header files (core module)
#include "core/coretypes.h"

namespace pov
{

/// One register-IR operation. Pure operations read virtual registers and
/// write one; a mask is a register holding 0.0 or 1.0 per lane. Terminators
/// (FnkBr, FnkBrCond, FnkRet) close a block and are the only jumps.
enum FnOpCode
{
    FnkConst,       // k = constant pool index                          -> dst
    FnkCopy,        // a                                               -> dst
    FnkAdd,         // a, b                                            -> dst
    FnkSub,         // a, b                                            -> dst
    FnkMul,         // a, b                                            -> dst
    FnkDiv,         // a, b                                            -> dst
    FnkMod,         // a, b                                            -> dst (fmod)
    FnkNeg,         // a                                               -> dst
    FnkAbs,         // a                                               -> dst
    FnkCmpGt,       // a, b                                            -> mask dst
    FnkCmpEq,       // a, b                                            -> mask dst
    FnkMaskOr,      // a, b masks                                      -> mask dst
    FnkMaskNot,     // a mask                                          -> mask dst
    FnkFlag,        // a mask                                          -> dst (0.0/1.0)
    FnkSelect,      // a mask, b if-set, c if-clear                    -> dst
    FnkAddK,        // a, k = constant pool index                      -> dst (a + k)
    FnkSubK,        // a - k
    FnkMulK,        // a * k
    FnkDivK,        // a / k
    FnkModK,        // fmod(a, k)
    FnkCmpGtKA,     // mask k > a
    FnkCmpEqKA,     // mask k == a
    FnkLocalGet,    // k = frame slot                                  -> dst
    FnkLocalSet,    // a, k = frame slot
    FnkSys1,        // k = sys1 id, a                                  -> dst (libm table)
    FnkSys2,        // k = sys2 id, a, b                               -> dst (libm table)
    FnkMath1,       // k = sys1 id, a                                  -> dst (xsimd kernels)
    FnkMath2,       // k = sys2 id, a, b                               -> dst (xsimd kernels)
    FnkTrap,        // k = trap id, a = first arg register, arity from the trap table, b|c<<16 = owning function -> dst
    FnkTrapS,       // k = trapS id, a = window base slot; reads and rewrites the window in place
    FnkCall,        // k = callee, a = first arg register, arity from the callee      -> dst
    FnkBr,          // k = block
    FnkBrCond,      // a = mask, k = taken block, b = clear block
    FnkRet          // a
};

struct FnOp final
{
    std::uint16_t code;
    std::uint16_t a;
    std::uint16_t b;
    std::uint16_t c;
    std::uint16_t dst;
    std::uint32_t k;
};

/// A lowered function: immutable once built. Values are double (F64); the
/// four-lane instantiation of the same definition keeps one value per lane.
struct FnDef final
{
    std::vector<FnOp> ops;                  // blocks in order, each closed by a terminator
    std::vector<unsigned int> blockStart;   // op index of each block entry
    std::vector<DBL> consts;                // FnkConst pool of this function
    unsigned int parameterCnt = 0;
    unsigned int argBase = 0;               // first parameter slot: a vector function's results precede its parameters
    unsigned int slotCnt = 0;               // frame slots: inputs, locals, temporaries, register spill homes
    unsigned int regCnt = 0;
};

}
// end of namespace pov

#endif // POVRAY_VM_FNIR_H
