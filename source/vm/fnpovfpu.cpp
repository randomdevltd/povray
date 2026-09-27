//******************************************************************************
///
/// @file vm/fnpovfpu.cpp
///
/// This module implements the virtual machine executing render-time functions.
///
/// This module is inspired by code by D. Skarda, T. Bily and R. Suzuki.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
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

/**

@file
@par Virtual Machine Instruction Set

# Rs = R0 - R7, one of eight floating-point registers
# Rd = R0 - R7, one of eight floating-point registers
# k = constant, can be DBL or int depending on function
# SP = data stack point
# PSP = program stack pointer
# CC = condition code register
# PC = program counter
# global = global variable data space
# local = local variable data space
# const = const floating-point value data space
# eq = equal
# ne = not equal
# lt = lower
# le = lower or equal
# gt = greater
# ge = greater or equal


R-Type Instructions (9 * 64 = 576)

 Opcode | Source |  Dest  |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 20 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   00   |   Rs   |   Rd   |   0000   | add   Rs, Rd     | Rd + Rs -> Rd
   01   |   Rs   |   Rd   |   0000   | sub   Rs, Rd     | Rd - Rs -> Rd
   02   |   Rs   |   Rd   |   0000   | mul   Rs, Rd     | Rd * Rs -> Rd
   03   |   Rs   |   Rd   |   0000   | div   Rs, Rd     | Rd / Rs -> Rd
   04   |   Rs   |   Rd   |   0000   | mod   Rs, Rd     | Rd % Rs -> Rd
   05   |   Rs   |   Rd   |   0000   | move  Rs, Rd     | Rs -> Rd
   06   |   Rs   |   Rd   |   0000   | cmp   Rs, Rd     | Rd - Rs -> CC
   07   |   Rs   |   Rd   |   0000   | neg   Rs, Rd     | -Rs -> Rd
   08   |   Rs   |   Rd   |   0000   | abs   Rs, Rd     | |Rs| -> Rd


I-Type Instructions (7 * 8 = 56)

 Opcode | Ext.Op |  Dest  |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 16 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   09   |   00   |   Rd   | const(k) | addi  const, Rd  | Rd + const(k) -> Rd
   09   |   01   |   Rd   | const(k) | subi  const, Rd  | Rd - const(k) -> Rd
   09   |   02   |   Rd   | const(k) | muli  const, Rd  | Rd * const(k) -> Rd
   09   |   03   |   Rd   | const(k) | divi  const, Rd  | Rd / const(k) -> Rd
   09   |   04   |   Rd   | const(k) | modi  const, Rd  | Rd % const(k) -> Rd
   09   |   05   |   Rd   | const(k) | loadi const, Rd  | const(k) -> Rd
   09   |   06   |   Rd   | const(k) | cmpi  const, Rd  | Rd - const(k) -> CC


JS-Type Instructions (6 * 8 = 48)

 Opcode | Ext.Op |  Dest  |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 16 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   10   |   00   |   Rd   |   0000   | seq   Rd         | (CC = eq) -> Rd
   10   |   01   |   Rd   |   0000   | sne   Rd         | (CC = ne) -> Rd
   10   |   02   |   Rd   |   0000   | slt   Rd         | (CC = lt) -> Rd
   10   |   03   |   Rd   |   0000   | sle   Rd         | (CC = le) -> Rd
   10   |   04   |   Rd   |   0000   | sgt   Rd         | (CC = gt) -> Rd
   10   |   05   |   Rd   |   0000   | sge   Rd         | (CC = ge) -> Rd
   10   |   06   |   Rd   |   0000   | teq   Rd         | (Rd == 0) -> Rd
   10   |   07   |   Rd   |   0000   | tne   Rd         | (Rd != 0) -> Rd


ML-Type Instructions (6 * 8 = 48)

 Opcode | Ext.Op |  Dest  |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 20 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   11   |   00   |   Rd   |   0000   | load 0(k), Rd    | global(k) -> Rd
   11   |   01   |   Rd   |   0000   | load SP(k), Rd   | local(k) -> Rd
   11   |   02   |   Rd   |   0000   | load 0(k,a), Rd  | global(k + a) -> Rd
   11   |   03   |   Rd   |   0000   | load SP(k,a), Rd | local(k + a) -> Rd
   11   |   04   |   Rd   |   0000   | load 0(k,b), Rd  | global(k + b) -> Rd
   11   |   05   |   Rd   |   0000   | load SP(k,b), Rd | local(k + b) -> Rd


MS-Type Instructions (6 * 8 = 48)

 Opcode | Ext.Op | Source |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 20 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   12   |   00   |   Rs   |   0000   | store Rs, 0(k)   | Rs -> global(k)
   12   |   01   |   Rs   |   0000   | store Rs, SP(k)  | Rs -> local(k)
   12   |   02   |   Rs   |   0000   | store Rs, 0(k,a) | Rs -> global(k + a)
   12   |   03   |   Rs   |   0000   | store Rs, SP(k,a)| Rs -> local(k + a)
   12   |   04   |   Rs   |   0000   | store Rs, 0(k,b) | Rs -> global(k + b)
   12   |   05   |   Rs   |   0000   | store Rs, SP(k,b)| Rs -> local(k + b)


JB-Type Instructions (6)

 Opcode | Ext.Op | Unused |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 20 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   13   |   00   |   00   |  offset  | beq   k          | if (CC = eq) then k -> PC
   13   |   01   |   00   |  offset  | bne   k          | if (CC = ne) then k -> PC
   13   |   02   |   00   |  offset  | blt   k          | if (CC = lt) then k -> PC
   13   |   03   |   00   |  offset  | ble   k          | if (CC = le) then k -> PC
   13   |   04   |   00   |  offset  | bgt   k          | if (CC = gt) then k -> PC
   13   |   05   |   00   |  offset  | bge   k          | if (CC = ge) then k -> PC


XS-Type Instructions (6 * 8 = 48)

 Opcode | Ext.Op | Source |  k-data  | Instruction      | Operation
 4 bits | 3 bits | 3 bits | 20 bits  |                  |
--------+--------+--------+----------+------------------+--------------------------------
   14   |   00   |   Rs   |   0000   | xeq   Rs         | if (Rs == 0) then exception
   14   |   01   |   Rs   |   0000   | xne   Rs         | if (Rs != 0) then exception
   14   |   02   |   Rs   |   0000   | xlt   Rs         | if (Rs < 0) then exception
   14   |   03   |   Rs   |   0000   | xle   Rs         | if (Rs <= 0) then exception
   14   |   04   |   Rs   |   0000   | xgt   Rs         | if (Rs > 0) then exception
   14   |   05   |   Rs   |   0000   | xge   Rs         | if (Rs >= 0) then exception
   14   |   06   |   Rs   |   0000   | xdz   R0, Rs     | if (R0 == 0) && (Rs == 0) then exception


X-Type Instructions (14)

 Opcode | Extended Opcode |  k-data  | Instruction      | Operation
 4 bits |     6 bits      | 20 bits  |                  |
--------+-----------------+----------+------------------+--------------------------------
   15   |       00        |   0000   | jsr   k          | PSP + 1 -> PSP, PC -> (PSP), k -> PC
   15   |       01        |   0000   | jmp   k          | k -> PC
   15   |       02        |   0000   | rts              | (PSP) -> PC, PSP - 1 -> PSP
   15   |       03        |   0000   | call  k          | calls user-defined function k, result will be in R0 or A
   15   |       04        |   0000   | sys1  k          | calls internal function k with one argument, result will be in R0 or A
   15   |       05        |   0000   | sys2  k          | calls internal function k with two arguments, result will be in R0 or A
   15   |       06        |   0000   | trap  k          | calls internal function k with arguments on stack, result will be in R0 or A
   15   |       07        |   0000   | traps k          | calls internal function k with arguments on stack, result will be on the stack
   15   |       08        |   0000   | grow  k          | grow the stack to hold k elements (integer or floating-point numbers)
   15   |       09        |   0000   | push  k          | move the stack pointer k elements forward
   15   |       10        |   0000   | pop   k          | move the stack pointer k elements backward
   15   |       11        |   0000   | iconv            | int(R0) -> A
   15   |       12        |   0000   | fconv            | float(A) -> R0
   15   |       31        |   0000   | nop              | no operation


XI-Type Instructions (4 * 18 = 72)

 Opcode | Ext.Op | Ext.Op |  k-data  | Instruction      | Operation
 4 bits | 6 bits | 2 bits | 20 bits  |                  |
--------+-----------------+----------+------------------+--------------------------------
   15   |   32   |   sd   |   0000   | add   s, d       | d + s -> d
   15   |   33   |   sd   |   0000   | sub   s, d       | d - s -> d
   15   |   34   |   sd   |   0000   | mul   s, d       | d * s -> d
   15   |   35   |   00   |   0000   | div   B, A       | A / B -> A
   15   |   35   |   01   |   0000   | div   A, B       | B / A -> B
   15   |   35   |   02   |   0000   | mod   B, A       | A % B -> A
   15   |   35   |   03   |   0000   | mod   A, B       | B % A -> B
   15   |   36   |   00   |   0000   | cmp   B, A       | A - B -> CC
   15   |   36   |   01   |   0000   | cmp   A, B       | B - A -> CC
   15   |   36   |   02   |   0000   | exg   A, B       | A <-> B
   15   |   36   |   03   |   0000   | clr   A, B       | 0 -> A, 0 -> B
   15   |   37   |   00   |   0000   | clr   A          | 0 -> A
   15   |   37   |   01   |   0000   | clr   B          | 0 -> B
   15   |   37   |   02   |   0000   | move  B, A       | B -> A
   15   |   37   |   03   |   0000   | move  A, B       | A -> B
   15   |   38   |   00   |   0000   | neg   A          | -A -> A
   15   |   38   |   01   |   0000   | neg   B          | -B -> B
   15   |   38   |   02   |   0000   | abs   A          | |A| -> A
   15   |   38   |   03   |   0000   | abs   B          | |B| -> B
   15   |   39   |   00   |   kkkk   | addi  k, A       | A + k -> A
   15   |   39   |   01   |   kkkk   | addi  k, B       | B + k -> B
   15   |   39   |   02   |   kkkk   | subi  k, A       | A - k -> A
   15   |   39   |   03   |   kkkk   | subi  k, B       | B - k -> B
   15   |   40   |   sd   |   0000   | asl   s, d       | d << s -> d (signed)
   15   |   41   |   sd   |   0000   | asr   s, d       | d >> s -> d (signed)
   15   |   42   |   sd   |   0000   | lsl   s, d       | d << s -> d (unsigned)
   15   |   43   |   sd   |   0000   | lsr   s, d       | d >> s -> d (unsigned)
   15   |   44   |   sd   |   0000   | and   s, d       | d & s -> d
   15   |   45   |   sd   |   0000   | or    s, d       | d | s -> d
   15   |   46   |   sd   |   0000   | xor   s, d       | d ^ s -> d
   15   |   47   |   sd   |   0000   | not   s, d       | !s -> d
   15   |   48   |   00   |   kkkk   | loadi A          | k -> A
   15   |   48   |   01   |   kkkk   | loadi B          | k -> B
   15   |   48   |   02   |   kkkk   | ldhi  A          | (A << 16) | k -> A
   15   |   48   |   03   |   kkkk   | ldhi  B          | (B << 16) | k -> B
   15   |   49   |   00   |   kkkk   | max   k, A       | max(k, A) -> A
   15   |   49   |   01   |   kkkk   | max   k, B       | max(k, B) -> B
   15   |   49   |   02   |   kkkk   | min   k, A       | min(k, A) -> A
   15   |   49   |   03   |   kkkk   | min   k, B       | min(k, B) -> B


Total number of defined instructions: 916
Density of instruction set: 916 / 1024 = 0.8945

*/

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "vm/fnpovfpu.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// C++ standard header files
#include <algorithm>

// POV-Ray header files (base module)
#include "base/mathutil.h"
#include "base/povassert.h"

// POV-Ray header files (core module)
#include "core/math/simd.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"

// POV-Ray header files (VM module)
#include "vm/fnintern.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;
using std::vector;

/*****************************************************************************
* Local preprocessor defines
******************************************************************************/

#define MAX_FN MAX_K

#ifndef POVFPU_THREADED
    #if defined(__GNUC__)
        #define POVFPU_THREADED 1
    #else
        #define POVFPU_THREADED 0
    #endif
#endif

/// Interpreter handlers; one list orders the decoder's codes, the threaded dispatch table and the switch.
#define POVFPU_OPS(X) \
    X(ADD) X(SUB) X(MUL) X(DIV) X(MOD) X(MOVE) X(CMP) X(NEG) X(ABS) \
    X(ADDI) X(SUBI) X(MULI) X(DIVI) X(MODI) X(LOADI) X(CMPI) \
    X(SEQ) X(SNE) X(SLT) X(SLE) X(SGT) X(SGE) X(TEQ) X(TNE) \
    X(LOADG) X(LOADL) X(STOREG) X(STOREL) \
    X(BEQ) X(BNE) X(BLT) X(BLE) X(BGT) X(BGE) \
    X(XEQ) X(XNE) X(XLT) X(XLE) X(XGT) X(XGE) X(XDZ) \
    X(JSR) X(JMP) X(RTS) X(CALL) X(CALLF) X(SYS1) X(SQRT) X(MATH1) X(SYS2) X(MATH2) X(TRAP) X(TRAPS) \
    X(GROW) X(PUSH) X(POP) X(NOP)

enum VMCode
{
#define POVFPU_ENUM(n) VM_##n,
    POVFPU_OPS(POVFPU_ENUM)
#undef POVFPU_ENUM
    VM_COUNT
};

/// Scalar handlers beyond the 1024 register-specialised opcodes.
enum { VMS_SQRT = 1024, VMS_MATH1, VMS_MATH2, VMS_CALLF, VMS_NOP, VMS_COUNT };

static VMOp DecodeInstruction(Instruction w, const FunctionCode& f)
{
    static const std::uint16_t kR[9] = { VM_ADD, VM_SUB, VM_MUL, VM_DIV, VM_MOD, VM_MOVE, VM_CMP, VM_NEG, VM_ABS };
    static const std::uint16_t kI[8] = { VM_ADDI, VM_SUBI, VM_MULI, VM_DIVI, VM_MODI, VM_LOADI, VM_CMPI, VM_NOP };
    static const std::uint16_t kS[8] = { VM_SEQ, VM_SNE, VM_SLT, VM_SLE, VM_SGT, VM_SGE, VM_TEQ, VM_TNE };
    static const std::uint16_t kM[4] = { VM_LOADG, VM_LOADL, VM_STOREG, VM_STOREL };
    static const std::uint16_t kB[8] = { VM_BEQ, VM_BNE, VM_BLT, VM_BLE, VM_BGT, VM_BGE, VM_NOP, VM_NOP };
    static const std::uint16_t kX[8] = { VM_XEQ, VM_XNE, VM_XLT, VM_XLE, VM_XGT, VM_XGE, VM_XDZ, VM_NOP };
    static const std::uint16_t kSpecial[16] = { VM_JSR, VM_JMP, VM_RTS, VM_CALL, VM_SYS1, VM_SYS2, VM_TRAP, VM_TRAPS,
                                                VM_GROW, VM_PUSH, VM_POP, VM_NOP, VM_NOP, VM_NOP, VM_NOP, VM_NOP };
    const unsigned int op = GET_OP(w), hi = op >> 6, mid = (op >> 3) & 7, lo = op & 7;
    VMOp d = { VM_NOP, std::uint16_t((op < 1024) ? op : VMS_NOP), std::uint8_t(mid), std::uint8_t(lo), GET_K(w) };

    if (hi < 9)
        d.code = kR[hi];
    else if (hi == 9)
        d.code = kI[mid];
    else if (hi == 10)
        d.code = kS[mid];
    else if (((hi == 11) || (hi == 12)) && (mid < 2))
        d.code = kM[(hi - 11) * 2 + mid];
    else if ((hi == 13) && (lo == 0))
        d.code = kB[mid];
    else if (hi == 14)
        d.code = kX[mid];
    else if ((hi == 15) && (mid < 2))
        d.code = kSpecial[mid * 8 + lo];

    if ((d.code == VM_SYS1) && (d.k == TRAP_SYS1_SQRT))
    {
        d.code = VM_SQRT;
        d.scalar = VMS_SQRT;
    }
    else if ((d.code == VM_SYS1) && (d.k <= TRAP_SYS1_LOG) && (d.k != TRAP_SYS1_FLOOR) && (d.k != TRAP_SYS1_CEIL))
    {
        d.code = VM_MATH1;
        d.scalar = VMS_MATH1;
    }
    else if ((d.code == VM_SYS2) && ((d.k == TRAP_SYS2_POW) || (d.k == TRAP_SYS2_ATAN2)))
    {
        d.code = VM_MATH2;
        d.scalar = VMS_MATH2;
    }
    else if ((d.code == VM_TRAP) && (d.k < POVFPU_TrapTableSize))
        d.a = std::uint8_t(POVFPU_TrapTable[d.k].parameter_cnt);
    else if (d.code == VM_TRAPS)
        d.a = std::uint8_t(f.return_size + f.parameter_cnt);
    return d;
}

/// Decodes a program, fusing each `push k; call; pop k` no branch lands inside, and ends it with a spare `rts`.
static void DecodeProgram(const FunctionCode& f, vector<VMOp>& ops)
{
    const unsigned int n = f.program_size;
    vector<bool> target(n + 1, false);

    ops.resize(n + 1);
    for (unsigned int i = 0; i < n; ++i)
    {
        ops[i] = DecodeInstruction(f.program[i], f);
        const unsigned int c = ops[i].code;
        if ((((c >= VM_BEQ) && (c <= VM_BGE)) || (c == VM_JMP) || (c == VM_JSR)) && (ops[i].k <= n))
            target[ops[i].k] = true;
    }
    ops[n] = VMOp { VM_RTS, OPCODE_RTS, 0, 0, 0 };
    for (unsigned int i = 0; i + 2 < n; ++i)
    {
        if ((ops[i].code == VM_PUSH) && (ops[i + 1].code == VM_CALL) && (ops[i + 2].code == VM_POP) &&
            (ops[i + 2].k == ops[i].k) && !target[i + 1] && !target[i + 2])
        {
            ops[i].code = VM_CALLF;
            ops[i].scalar = VMS_CALLF;
        }
    }
}

/*****************************************************************************
* Local typedefs
******************************************************************************/


/*****************************************************************************
* Local functions
******************************************************************************/

SYS_MATH_RETURN math_int(SYS_MATH_PARAM i);
SYS_MATH_RETURN math_div(SYS_MATH_PARAM i1, SYS_MATH_PARAM i2);


/*****************************************************************************
* Global variables
******************************************************************************/

const Opcode POVFPU_Opcodes[] =
{
    { "add",   OPCODE_ADD,    ITYPE_R },
    { "sub",   OPCODE_SUB,    ITYPE_R },
    { "mul",   OPCODE_MUL,    ITYPE_R },
    { "div",   OPCODE_DIV,    ITYPE_R },
    { "mod",   OPCODE_MOD,    ITYPE_R },
    { "move",  OPCODE_MOVE,   ITYPE_R },
    { "cmp",   OPCODE_CMP,    ITYPE_R },
    { "neg",   OPCODE_NEG,    ITYPE_R },
    { "abs",   OPCODE_ABS,    ITYPE_R },
    { "addi",  OPCODE_ADDI,   ITYPE_I },
    { "subi",  OPCODE_SUBI,   ITYPE_I },
    { "muli",  OPCODE_MULI,   ITYPE_I },
    { "divi",  OPCODE_DIVI,   ITYPE_I },
    { "modi",  OPCODE_MODI,   ITYPE_I },
    { "loadi", OPCODE_LOADI,  ITYPE_I },
    { "cmpi",  OPCODE_CMPI,   ITYPE_I },
    { "seq",   OPCODE_SEQ,    ITYPE_S },
    { "sne",   OPCODE_SNE,    ITYPE_S },
    { "slt",   OPCODE_SLT,    ITYPE_S },
    { "sle",   OPCODE_SLE,    ITYPE_S },
    { "sgt",   OPCODE_SGT,    ITYPE_S },
    { "sge",   OPCODE_SGE,    ITYPE_S },
    { "teq",   OPCODE_TEQ,    ITYPE_S },
    { "tne",   OPCODE_TNE,    ITYPE_S },
    { "load",  OPCODE_LOAD,   ITYPE_M },
    { "store", OPCODE_STORE,  ITYPE_M },
    { "xeq",   OPCODE_XEQ,    ITYPE_S },
    { "xne",   OPCODE_XNE,    ITYPE_S },
    { "xlt",   OPCODE_XLT,    ITYPE_S },
    { "xle",   OPCODE_XLE,    ITYPE_S },
    { "xgt",   OPCODE_XGT,    ITYPE_S },
    { "xge",   OPCODE_XGE,    ITYPE_S },
    { "xdz",   OPCODE_XDZ,    ITYPE_S },
    { "beq",   OPCODE_BEQ,    ITYPE_J },
    { "bne",   OPCODE_BNE,    ITYPE_J },
    { "blt",   OPCODE_BLT,    ITYPE_J },
    { "ble",   OPCODE_BLE,    ITYPE_J },
    { "bgt",   OPCODE_BGT,    ITYPE_J },
    { "bge",   OPCODE_BGE,    ITYPE_J },
    { "jsr",   OPCODE_JSR,    ITYPE_J },
    { "jmp",   OPCODE_JMP,    ITYPE_J },
    { "rts",   OPCODE_RTS,    ITYPE_X },
    { "call",  OPCODE_CALL,   ITYPE_J },
    { "sys1",  OPCODE_SYS1,   ITYPE_J },
    { "sys2",  OPCODE_SYS2,   ITYPE_J },
    { "trap",  OPCODE_TRAP,   ITYPE_J },
    { "traps", OPCODE_TRAPS,  ITYPE_J },
    { "grow",  OPCODE_GROW,   ITYPE_J },
    { "push",  OPCODE_PUSH,   ITYPE_J },
    { "pop",   OPCODE_POP,    ITYPE_J },
    { "nop",   OPCODE_NOP,    ITYPE_X },
    { nullptr, 0, 0 }
};

const Sys1 POVFPU_Sys1Table[] =
{
    sin,            // 0
    cos,            // 1
    tan,            // 2
    asin,           // 3
    acos,           // 4
    atan,           // 5
    sinh,           // 6
    cosh,           // 7
    tanh,           // 8
    std::asinh,     // 9
    std::acosh,     // 10
    std::atanh,     // 11
    floor,          // 12
    ceil,           // 13
    sqrt,           // 14
    exp,            // 15
    log,            // 16
    log10,          // 17
    math_int,       // 18
    nullptr
};

const Sys2 POVFPU_Sys2Table[] =
{
    pow,            // 0
    atan2,          // 1
    fmod,           // 2
    math_div,       // 3
    nullptr
};


const unsigned int POVFPU_Sys1TableSize = 19;
const unsigned int POVFPU_Sys2TableSize = 4;

/*****************************************************************************
*
* FUNCTION
*
*   math_int
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Misc. math utility functions.
*
* CHANGES
*
*   -
*
******************************************************************************/

SYS_MATH_RETURN math_int(SYS_MATH_PARAM i)
{
    return (SYS_MATH_RETURN)((int)(i));
}


/*****************************************************************************
*
* FUNCTION
*
*   math_div
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Misc. math utility functions.
*
* CHANGES
*
*   -
*
******************************************************************************/

SYS_MATH_RETURN math_div(SYS_MATH_PARAM i1, SYS_MATH_PARAM i2)
{
    return (SYS_MATH_RETURN)((int)(i1/i2));
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_Init
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Init the virtual machine.
*
* CHANGES
*
*   -
*
******************************************************************************/

FunctionVM::FunctionVM()
{
    // default constants are 0 and 1
    AddConstant(0.0);
    AddConstant(1.0);

    nextUnreferenced = MAX_FN;
    SYS_INIT_FUNCTIONS();
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_Terminate
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Terminate the virtual machine.  Automatically performs a reset.
*
* CHANGES
*
*   -
*
******************************************************************************/

FunctionVM::~FunctionVM()
{
    SYS_TERM_FUNCTIONS();

    Reset();
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_Reset
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Reset the virtual machine.  This clears all global variables, constants
*   and functions.
*
* CHANGES
*
*   -
*
******************************************************************************/

void FunctionVM::Reset()
{
    SYS_RESET_FUNCTIONS();

    globals.clear();
    consts.clear();

    for(vector<FunctionEntry>::iterator i(functions.begin()); i != functions.end(); i++)
    {
        if(i->reference_count > 0) // ignore the reference count [trf]
        {
            SYS_DELETE_FUNCTION(&(*i));
            FNCode_Delete(&(i->fn));
            i->reference_count = 0;
        }
    }

    functions.clear();
    nextUnreferenced = MAX_FN;
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_SetLocal
*
* INPUT
*
*   k - stack offset
*   v - floating-point value
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Set a floating-point value on the stack position. Grow the stack if
*   necessary.
*
* CHANGES
*
*   -
*
******************************************************************************/

void FPUContext::SetLocal(unsigned int k, DBL v)
{
    if(k >= maxdblstacksize)
    {
        #if (SYS_FUNCTIONS == 1)
        size_t diff = ((size_t)(dblstack)) - ((size_t)(dblstackbase));
        #endif

        maxdblstacksize = max(k + 1, (unsigned int)INITIAL_DBL_STACK_SIZE);
        dblstackbase = reinterpret_cast<DBL *>(POV_REALLOC(dblstackbase, sizeof(DBL) * maxdblstacksize, "fn: stack"));

        #if (SYS_FUNCTIONS == 1)
        dblstack = reinterpret_cast<DBL *>(((size_t)(dblstackbase)) + diff);
        #endif
    }

    dblstackbase[k] = v;
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_GetLocal
*
* INPUT
*
*   k - stack offset
*
* OUTPUT
*
* RETURNS
*
*   DBL value at stack position k
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Get a floating-point value from the stack position.
*
* CHANGES
*
*   -
*
******************************************************************************/

DBL FPUContext::GetLocal(unsigned int k)
{
    if(k >= maxdblstacksize)
        return 0.0;

    return dblstackbase[k];
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_SetGlobal
*
* INPUT
*
*   k - global variable reference number
*   v - floating-point number
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Set the global variable's value.
*
* CHANGES
*
*   -
*
******************************************************************************/

void FunctionVM::SetGlobal(unsigned int k, DBL v)
{
    if(globals.size() < k + 1)
        globals.resize(k + 1);

    globals[k] = v;
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_GetGlobal
*
* INPUT
*
*   k - global variable reference number
*
* OUTPUT
*
* RETURNS
*
*   DBL value of global variable k
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Get the global variable's value.
*
* CHANGES
*
*   -
*
******************************************************************************/

DBL FunctionVM::GetGlobal(unsigned int k)
{
    if(k >= globals.size())
        return 0.0;

    return globals[k];
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_GetFunction
*
* INPUT
*
*   k - function reference number
*
* OUTPUT
*
* RETURNS
*
*   FunctionCode - pointer to function code
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Access the FunctionCode of a function by its reference number.
*
* CHANGES
*
*   -
*
******************************************************************************/

FunctionCode *FunctionVM::GetFunction(FUNCTION k)
{
    if(k >= functions.size())
        throw POV_EXCEPTION_STRING("Unknown user defined function.");

    return &(functions[k].fn);
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_GetFunctionAndReference
*
* INPUT
*
*   k - function reference number
*
* OUTPUT
*
* RETURNS
*
*   FunctionCode - pointer to function code
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Access the FunctionCode of a function by its reference number and
*   increase the function reference count by 1.
*
* CHANGES
*
*   -
*
******************************************************************************/

FunctionCode *FunctionVM::GetFunctionAndReference(FUNCTION k)
{
    if(k >= functions.size())
        throw POV_EXCEPTION_STRING("Unknown user defined function.");

    functions[k].reference_count++;

    return &(functions[k].fn);
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_AddConstant
*
* INPUT
*
*   v - floating-point constant value
*
* OUTPUT
*
* RETURNS
*
*   unsigned int - floating-point constant reference number
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Add a floating-point constant to the table of constants.
*
* CHANGES
*
*   -
*
******************************************************************************/

unsigned int FunctionVM::AddConstant(DBL v)
{
    unsigned int i;

    for(i = 0; i < consts.size(); i++)
    {
        if(consts[i] == v)
            return (unsigned int)i;
    }

    if(consts.size() == MAX_K)
        throw POV_EXCEPTION_STRING("More than 1048576 constants in all functions are not supported.");

    consts.push_back(v);

    return consts.size() - 1;
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_AddFunction
*
* INPUT
*
*   f - function to work on
*
* OUTPUT
*
* RETURNS
*
*   unsigned int - function reference number
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Add a function to the virtual machine memory.
*
* CHANGES
*
*   -
*
******************************************************************************/

FUNCTION FunctionVM::AddFunction(FunctionCode *f)
{
    FUNCTION fn = 0;

    if (nextUnreferenced < functions.size())
    {
        // re-use an unreferenced entry if possible
        fn = nextUnreferenced;
        nextUnreferenced = functions[fn].next_unreferenced;
    }
    else if (functions.size() < MAX_FN)
    {
        functions.push_back(FunctionEntry());
        fn = functions.size() - 1;
    }
    else
        throw POV_EXCEPTION_STRING("Maximum number of 1046576 functions per scene reached.");

    functions[fn].fn = *f;
    DecodeProgram(functions[fn].fn, functions[fn].ops);
    functions[fn].reference_count = 1;
    SYS_ADD_FUNCTION(fn);

    return fn;
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_RemoveFunction
*
* INPUT
*
*   fn - function reference number
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Removes a function from the virtual machine memory.  It also removes
*   all functions that are no longer used.
*
* CHANGES
*
*   -
*
******************************************************************************/

void FunctionVM::RemoveFunction(FUNCTION fn)
{
    if(fn >= functions.size())
        return;

    if(functions[fn].reference_count > 0) // necessary to prevent any recursion
    {
        functions[fn].reference_count--;

        if(functions[fn].reference_count == 0)
        {
            FunctionEntry f = functions[fn];
            unsigned int i = 0;

            SYS_DELETE_FUNCTION(&f);
            for(i = 0; i < f.fn.program_size; i++)
            {
                if(GET_OP(f.fn.program[i]) == OPCODE_CALL)
                    RemoveFunction(GET_K(f.fn.program[i]));
            }
            FNCode_Delete(&(f.fn));
            vector<VMOp>().swap(functions[fn].ops);

            // we use unused entries to store a linked list of those, for easier later re-use
            functions[fn].next_unreferenced = nextUnreferenced;
            nextUnreferenced = fn;
        }
    }
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_Exception
*
* INPUT
*
*   fn - function reference number
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Shows a floating-point exception error message.
*
* CHANGES
*
*   -
*
******************************************************************************/

void POVFPU_Exception(FPUContext *context, FUNCTION fn, const char *msg)
{
    vector<FunctionEntry>& functions(context->functionvm->functions);

    if(!functions[fn].fn.sourceInfo.name.empty())
    {
        if (msg != nullptr)
;// TODO MESSAGE            ErrorAt(functions[fn].fn.sourceInfo,
//                  "Runtime error detected in function '%s'. %s", functions[fn].fn.name, msg);
        else
;// TODO MESSAGE            ErrorAt(functions[fn].fn.sourceInfo,
//                  "Floating-point exception detected in function '%s'. "
//                  "Your function either attempted a division by zero, used a function outside its "
//                  "domain or called an internal function with invalid parameters.",
//                  functions[fn].fn.name);
    }
    else
    {
        if (msg != nullptr)
;// TODO MESSAGE            ErrorAt(functions[fn].fn.sourceInfo,
//                  "Runtime error detected in function. %s", msg);
        else
;// TODO MESSAGE            ErrorAt(functions[fn].fn.sourceInfo,
//                  "Floating-point exception detected in unnamed function. "
//                  "Your function either attempted a division by zero, used a function outside its "
//                  "domain or called an internal function with invalid parameters.");
    }
}


/*****************************************************************************
*
* FUNCTION
*
*   POVFPU_RunDefault
*
* INPUT
*
*   fn - function reference number
*
* OUTPUT
*
* RETURNS
*
*   DBL - result found in R0
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Execute a compiled function.
*
* CHANGES
*
*   Runs the decoded program with one indirect jump per handler where the compiler has computed goto.
*
******************************************************************************/

typedef simd::Vec<DBL, POVFPU_LANES> LaneVec;
typedef LaneVec::MaskType LaneMask;

/// The library functions both interpreters take from the simd layer, so one point and a batch agree bit for bit.
static LaneVec Math1(unsigned int k, LaneVec a)
{
    switch (k)
    {
        case TRAP_SYS1_SIN:     return simd::sin(a);
        case TRAP_SYS1_COS:     return simd::cos(a);
        case TRAP_SYS1_TAN:     return simd::tan(a);
        case TRAP_SYS1_ASIN:    return simd::asin(a);
        case TRAP_SYS1_ACOS:    return simd::acos(a);
        case TRAP_SYS1_ATAN:    return simd::atan(a);
        case TRAP_SYS1_SINH:    return simd::sinh(a);
        case TRAP_SYS1_COSH:    return simd::cosh(a);
        case TRAP_SYS1_TANH:    return simd::tanh(a);
        case TRAP_SYS1_ASINH:   return simd::asinh(a);
        case TRAP_SYS1_ACOSH:   return simd::acosh(a);
        case TRAP_SYS1_ATANH:   return simd::atanh(a);
        case TRAP_SYS1_EXP:     return simd::exp(a);
        case TRAP_SYS1_LN:      return simd::log(a);
        default:                return simd::log10(a);
    }
}

static LaneVec Math2(unsigned int k, LaneVec a, LaneVec b)
{
    return (k == TRAP_SYS2_POW) ? simd::pow(a, b) : simd::atan2(a, b);
}

static DBL FirstLane(LaneVec v)
{
    DBL out[POVFPU_LANES];
    v.store(out);
    return out[0];
}

// Scalar handlers, one per opcode with its register fields, as the original switch had: h opcode, m and l fields.
#define POVFPU_EACH_L(X, h, m) X(h, m, 0) X(h, m, 1) X(h, m, 2) X(h, m, 3) X(h, m, 4) X(h, m, 5) X(h, m, 6) X(h, m, 7)
#define POVFPU_EACH_ML(X, h) POVFPU_EACH_L(X, h, 0) POVFPU_EACH_L(X, h, 1) POVFPU_EACH_L(X, h, 2) POVFPU_EACH_L(X, h, 3) \
    POVFPU_EACH_L(X, h, 4) POVFPU_EACH_L(X, h, 5) POVFPU_EACH_L(X, h, 6) POVFPU_EACH_L(X, h, 7)
#define POVFPU_EACH(X) POVFPU_EACH_ML(X, 0) POVFPU_EACH_ML(X, 1) POVFPU_EACH_ML(X, 2) POVFPU_EACH_ML(X, 3) \
    POVFPU_EACH_ML(X, 4) POVFPU_EACH_ML(X, 5) POVFPU_EACH_ML(X, 6) POVFPU_EACH_ML(X, 7) POVFPU_EACH_ML(X, 8) \
    POVFPU_EACH_ML(X, 9) POVFPU_EACH_ML(X, 10) POVFPU_EACH_ML(X, 11) POVFPU_EACH_ML(X, 12) POVFPU_EACH_ML(X, 13) \
    POVFPU_EACH_ML(X, 14) POVFPU_EACH_ML(X, 15)

#define POVFPU_CMP(c, d) ccr = ((((c) > (d)) & 1) << 1) | (((c) == (d)) & 1)
#define POVFPU_EXC(cond) if (cond) POVFPU_Exception(context, fn)

#define POVFPU_BODY_0(m, l) r##l = r##l + r##m;
#define POVFPU_BODY_1(m, l) r##l = r##l - r##m;
#define POVFPU_BODY_2(m, l) r##l = r##l * r##m;
#define POVFPU_BODY_3(m, l) r##l = r##l / r##m;
#define POVFPU_BODY_4(m, l) r##l = fmod(r##l, r##m);
#define POVFPU_BODY_5(m, l) r##l = r##m;
#define POVFPU_BODY_6(m, l) POVFPU_CMP(r##m, r##l);
#define POVFPU_BODY_7(m, l) r##l = -r##m;
#define POVFPU_BODY_8(m, l) r##l = fabs(r##m);
#define POVFPU_BODY_9(m, l) POVFPU_I##m(l)
#define POVFPU_BODY_10(m, l) POVFPU_S##m(l)
#define POVFPU_BODY_11(m, l) POVFPU_LOAD##m(l)
#define POVFPU_BODY_12(m, l) POVFPU_STORE##m(l)
#define POVFPU_BODY_13(m, l) POVFPU_B##l(m)
#define POVFPU_BODY_14(m, l) POVFPU_X##m(l)
#define POVFPU_BODY_15(m, l) POVFPU_SP##m(l)

#define POVFPU_I0(l) r##l = r##l + consts[op->k];
#define POVFPU_I1(l) r##l = r##l - consts[op->k];
#define POVFPU_I2(l) r##l = r##l * consts[op->k];
#define POVFPU_I3(l) r##l = r##l / consts[op->k];
#define POVFPU_I4(l) r##l = fmod(r##l, consts[op->k]);
#define POVFPU_I5(l) r##l = consts[op->k];
#define POVFPU_I6(l) POVFPU_CMP(consts[op->k], r##l);
#define POVFPU_I7(l)
#define POVFPU_S0(l) r##l = (ccr == 1);
#define POVFPU_S1(l) r##l = (ccr != 1);
#define POVFPU_S2(l) r##l = (ccr == 2);
#define POVFPU_S3(l) r##l = (ccr >= 1);
#define POVFPU_S4(l) r##l = (ccr == 0);
#define POVFPU_S5(l) r##l = (ccr <= 1);
#define POVFPU_S6(l) r##l = (r##l == 0.0);
#define POVFPU_S7(l) r##l = (r##l != 0.0);
#define POVFPU_LOAD0(l) r##l = globals[op->k];
#define POVFPU_LOAD1(l) r##l = dblstack[sp + op->k];
#define POVFPU_STORE0(l) globals[op->k] = r##l;
#define POVFPU_STORE1(l) dblstack[sp + op->k] = r##l;
#define POVFPU_LOAD2(l)
#define POVFPU_LOAD3(l)
#define POVFPU_LOAD4(l)
#define POVFPU_LOAD5(l)
#define POVFPU_LOAD6(l)
#define POVFPU_LOAD7(l)
#define POVFPU_STORE2(l)
#define POVFPU_STORE3(l)
#define POVFPU_STORE4(l)
#define POVFPU_STORE5(l)
#define POVFPU_STORE6(l)
#define POVFPU_STORE7(l)
#define POVFPU_B0(m) POVFPU_BR##m
#define POVFPU_B1(m)
#define POVFPU_B2(m)
#define POVFPU_B3(m)
#define POVFPU_B4(m)
#define POVFPU_B5(m)
#define POVFPU_B6(m)
#define POVFPU_B7(m)
#define POVFPU_BR0 if (ccr == 1) VM_JUMP(op->k);
#define POVFPU_BR1 if (ccr != 1) VM_JUMP(op->k);
#define POVFPU_BR2 if (ccr == 2) VM_JUMP(op->k);
#define POVFPU_BR3 if (ccr >= 1) VM_JUMP(op->k);
#define POVFPU_BR4 if (ccr == 0) VM_JUMP(op->k);
#define POVFPU_BR5 if (ccr <= 1) VM_JUMP(op->k);
#define POVFPU_BR6
#define POVFPU_BR7
#define POVFPU_X0(l) POVFPU_EXC(r##l == 0.0);
#define POVFPU_X1(l) POVFPU_EXC(r##l != 0.0);
#define POVFPU_X2(l) POVFPU_EXC(r##l < 0.0);
#define POVFPU_X3(l) POVFPU_EXC(r##l <= 0.0);
#define POVFPU_X4(l) POVFPU_EXC(r##l > 0.0);
#define POVFPU_X5(l) POVFPU_EXC(r##l >= 0.0);
#define POVFPU_X6(l) POVFPU_EXC((r0 == 0.0) && (r##l == 0.0));
#define POVFPU_X7(l)
#define POVFPU_SP0(l) POVFPU_SPECIAL##l
#define POVFPU_SP1(l) POVFPU_STACK##l
#define POVFPU_SP2(l)
#define POVFPU_SP3(l)
#define POVFPU_SP4(l)
#define POVFPU_SP5(l)
#define POVFPU_SP6(l)
#define POVFPU_SP7(l)
#define POVFPU_SPECIAL0 POVFPU_PUSH_FRAME(); VM_JUMP(op->k);
#define POVFPU_SPECIAL1 VM_JUMP(op->k);
#define POVFPU_SPECIAL2 \
    if (psp == 0) \
        return r0; \
    psp--; \
    pc = pstack[psp].pc; \
    fn = pstack[psp].fn; \
    ops = functions[fn].ops.data();
#define POVFPU_SPECIAL3 POVFPU_PUSH_FRAME(); fn = op->k; ops = functions[fn].ops.data(); VM_JUMP(0);
#define POVFPU_SPECIAL4 r0 = POVFPU_Sys1Table[op->k](r0);
#define POVFPU_SPECIAL5 r0 = POVFPU_Sys2Table[op->k](r0, r1);
#define POVFPU_SPECIAL6 \
    r0 = POVFPU_TrapTable[op->k].fn(context, &dblstack[sp], fn); \
    maxdblstacksize = context->maxdblstacksize; \
    dblstack = context->dblstackbase;
#define POVFPU_SPECIAL7 \
    POVFPU_TrapSTable[op->k].fn(context, &dblstack[sp], fn, sp); \
    maxdblstacksize = context->maxdblstacksize; \
    dblstack = context->dblstackbase;
#define POVFPU_STACK0 \
    if ((unsigned int)(sp + op->k) >= (unsigned int)MAX_K) \
        POVFPU_Exception(context, fn, "Stack full. Possible infinite recursive function call."); \
    else if (sp + op->k >= maxdblstacksize) \
    { \
        maxdblstacksize = context->maxdblstacksize = context->maxdblstacksize + max(op->k + 1, (unsigned int)INITIAL_DBL_STACK_SIZE); \
        dblstack = context->dblstackbase = reinterpret_cast<DBL *>(POV_REALLOC(dblstack, sizeof(DBL) * maxdblstacksize, "fn: stack")); \
    }
#define POVFPU_STACK1 \
    if (sp + op->k >= maxdblstacksize) \
        POVFPU_Exception(context, fn, "Function evaluation stack overflow."); \
    sp += op->k;
#define POVFPU_STACK2 \
    if (op->k > sp) \
        POVFPU_Exception(context, fn, "Function evaluation stack underflow."); \
    sp -= op->k;
#define POVFPU_STACK3
#define POVFPU_STACK4
#define POVFPU_STACK5
#define POVFPU_STACK6
#define POVFPU_STACK7

#define POVFPU_PUSH_FRAME() \
    pstack[psp].pc = pc; \
    pstack[psp].fn = fn; \
    psp++; \
    if (psp >= MAX_CALL_STACK_SIZE) \
        POVFPU_Exception(context, fn, "Maximum function evaluation recursion level reached.")

DBL POVFPU_RunDefault(FPUContext *context, FUNCTION fn)
{
    context->threaddata->Stats()[Ray_Function_VM_Calls]++;
    return POVFPU_RunScalar(context, fn);
}

#if POVFPU_THREADED && defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("no-crossjumping", "no-gcse", "no-tree-slp-vectorize")))
#endif
DBL POVFPU_RunScalar(FPUContext *context, FUNCTION fn)
{
    vector<FunctionEntry>& functions(context->functionvm->functions);
    const DBL *consts = context->functionvm->consts.data();
    vector<DBL>& globals(context->functionvm->globals);
    StackFrame *pstack = context->pstackbase;
    DBL *dblstack = context->dblstackbase;
    unsigned int maxdblstacksize = context->maxdblstacksize;
    DBL r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, r4 = 0.0, r5 = 0.0, r6 = 0.0, r7 = 0.0;
    const VMOp *ops = functions[fn].ops.data();
    const VMOp *op = ops;
    unsigned int pc = 0;
    unsigned int ccr = 0;
    unsigned int sp = 0;
    unsigned int psp = 0;

#if POVFPU_THREADED
    #define POVFPU_LABEL(h, m, l) &&L_##h##_##m##_##l,
    static const void *const dispatch[VMS_COUNT] =
    {
        POVFPU_EACH(POVFPU_LABEL) &&L_SQRT, &&L_MATH1, &&L_MATH2, &&L_CALLF, &&L_NOP
    };
    #undef POVFPU_LABEL
    #define VM_CASE(h, m, l) L_##h##_##m##_##l:
    #define VM_SPECIAL(name, index) L_##name:
    #define VM_NEXT() { op = ops + ++pc; goto *dispatch[op->scalar]; }
    #define VM_JUMP(t) { pc = (t); op = ops + pc; goto *dispatch[op->scalar]; }
    goto *dispatch[op->scalar];
#else
    #define VM_CASE(h, m, l) case ((h) * 64 + (m) * 8 + (l)):
    #define VM_SPECIAL(name, index) case index:
    #define VM_NEXT() { ++pc; continue; }
    #define VM_JUMP(t) { pc = (t); continue; }
    for (;;)
    {
    op = ops + pc;
    switch (op->scalar)
    {
#endif

    #define POVFPU_HANDLER(h, m, l) VM_CASE(h, m, l) POVFPU_BODY_##h(m, l) VM_NEXT();
    POVFPU_EACH(POVFPU_HANDLER)
    #undef POVFPU_HANDLER

    VM_SPECIAL(SQRT, VMS_SQRT)      r0 = sqrt(r0); VM_NEXT();
    VM_SPECIAL(MATH1, VMS_MATH1)    r0 = FirstLane(Math1(op->k, LaneVec(r0))); VM_NEXT();
    VM_SPECIAL(MATH2, VMS_MATH2)    r0 = FirstLane(Math2(op->k, LaneVec(r0), LaneVec(r1))); VM_NEXT();
    VM_SPECIAL(CALLF, VMS_CALLF)
        if (sp + op->k >= maxdblstacksize)
            POVFPU_Exception(context, fn, "Function evaluation stack overflow.");
        sp += op->k;
        op = ops + ++pc;
        POVFPU_PUSH_FRAME();
        fn = op->k;
        ops = functions[fn].ops.data();
        VM_JUMP(0);
    VM_SPECIAL(NOP, VMS_NOP)
#if !POVFPU_THREADED
    default:
#endif
        VM_NEXT();

#if !POVFPU_THREADED
    }
    }
#endif
    #undef VM_CASE
    #undef VM_SPECIAL
    #undef VM_NEXT
    #undef VM_JUMP
}

static_assert(POVFPU_LANES == 4, "kLaneOn lists four lanes");
static const unsigned int kAllLanes = (1u << POVFPU_LANES) - 1;
static const DBL kLaneOn[1 << POVFPU_LANES][POVFPU_LANES] =
{
    { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 1, 0, 0 }, { 0, 0, 1, 0 }, { 1, 0, 1, 0 }, { 0, 1, 1, 0 }, { 1, 1, 1, 0 },
    { 0, 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 1, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 0, 1, 1 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 }
};

static inline LaneMask MaskOf(unsigned int lanes) { return LaneVec::load(kLaneOn[lanes]) != LaneVec(0.0); }

/// Runs a program for four points at once, each lane computing exactly what the scalar interpreter would.
/// Lanes that branch apart keep their own program counters; the lowest runs next, so they meet again at joins.
struct BatchRun final
{
    FPUContext *ctx;
    FunctionVM *vm;
    unsigned int unique;    // lanes [unique, 4) repeat lane unique - 1, so only the first `unique` call out
    DBL *ls;                // lane stack: slot s of lane i at ls[s * 4 + i]
    LaneVec r[8];
    LaneMask lt;            // lanes whose last compare set "lower"
    LaneMask eq;            // lanes whose last compare set "equal"

    BatchRun(FPUContext *c, unsigned int u) : ctx(c), vm(c->functionvm.get()), unique(u), ls(c->lanestackbase)
    {
        for (int i = 0; i < 8; ++i)
            r[i] = LaneVec(0.0);
        lt = eq = (LaneVec(0.0) != LaneVec(0.0));
    }

    bool Reserve(unsigned int slots)
    {
        if (slots >= MAX_K)
            return false;
        if (slots > ctx->maxlanestacksize)
        {
            const unsigned int size = max(slots, ctx->maxlanestacksize + (unsigned int)INITIAL_DBL_STACK_SIZE);
            DBL *p = reinterpret_cast<DBL *>(POV_MALLOC(sizeof(DBL) * POVFPU_LANES * size, "fn: lane stack"));
            if (ctx->lanestackbase != nullptr)
            {
                std::memcpy(p, ctx->lanestackbase, sizeof(DBL) * POVFPU_LANES * ctx->maxlanestacksize);
                POV_FREE(ctx->lanestackbase);
            }
            ctx->lanestackbase = p;
            ctx->maxlanestacksize = size;
        }
        ls = ctx->lanestackbase;
        return true;
    }

    POV_SIMD_INLINE void Put(unsigned int reg, LaneVec v, unsigned int act)
    {
        r[reg] = (act == kAllLanes) ? v : simd::select(MaskOf(act), v, r[reg]);
    }

    POV_SIMD_INLINE void Compare(LaneVec c, LaneVec d, unsigned int act)
    {
        if (act == kAllLanes)
        {
            lt = (c > d);
            eq = (c == d);
        }
        else
        {
            const LaneMask m = MaskOf(act);
            lt = (m & (c > d)) | (~m & lt);
            eq = (m & (c == d)) | (~m & eq);
        }
    }

    static POV_SIMD_INLINE LaneVec Flag(LaneMask m) { return simd::select(m, LaneVec(1.0), LaneVec(0.0)); }

    /// Calls `f(lane values...)` for the active lanes that hold distinct points; repeats copy lane unique - 1.
    template<typename F>
    void PerLane(unsigned int act, DBL *v, F f) const
    {
        for (unsigned int i = 0; i < POVFPU_LANES; ++i)
        {
            if (act & (1u << i))
                v[i] = (i < unique) ? f(i) : v[unique - 1];
        }
    }

    bool CallTrap(FUNCTION fn, const VMOp& op, unsigned int sp, unsigned int act, bool vectorResult);
    bool Frame(FUNCTION fn, unsigned int sp, unsigned int own, int depth);
};

bool BatchRun::CallTrap(FUNCTION fn, const VMOp& op, unsigned int sp, unsigned int act, bool vectorResult)
{
    const unsigned int window = op.a;
    if (sp + window > ctx->maxlanestacksize)
        return false;
    if ((window > 0) && (window > ctx->maxdblstacksize))
        ctx->SetLocal(window - 1, 0.0);

    DBL result[POVFPU_LANES];
    r[0].store(result);
    for (unsigned int i = 0; i < POVFPU_LANES; ++i)
    {
        if (!(act & (1u << i)))
            continue;
        const unsigned int src = min(i, unique - 1);
        if (i == src)
        {
            for (unsigned int j = 0; j < window; ++j)
                ctx->dblstackbase[j] = ls[(sp + j) * POVFPU_LANES + i];
            if (vectorResult)
                POVFPU_TrapSTable[op.k].fn(ctx, ctx->dblstackbase, fn, 0);
            else
                result[i] = POVFPU_TrapTable[op.k].fn(ctx, ctx->dblstackbase, fn);
        }
        else if (!vectorResult)
            result[i] = result[src];
        if (vectorResult)
        {
            for (unsigned int j = 0; j < window; ++j)
                ls[(sp + j) * POVFPU_LANES + i] = (i == src) ? ctx->dblstackbase[j] : ls[(sp + j) * POVFPU_LANES + src];
        }
    }
    if (!vectorResult)
        r[0] = LaneVec::load(result);
    return true;
}

bool BatchRun::Frame(FUNCTION fn, unsigned int sp, unsigned int own, int depth)
{
    if (depth >= 64)
        return false;

    const vector<DBL>& consts(vm->consts);
    const vector<DBL>& globals(vm->globals);
    const VMOp *ops = vm->functions[fn].ops.data();
    unsigned int pc = 0;
    unsigned int live = own;
    unsigned int act = own;
    unsigned int lanePc[POVFPU_LANES] = { 0, 0, 0, 0 };
    bool diverged = false;
    DBL v[POVFPU_LANES];
    DBL w[POVFPU_LANES];

    for (;;)
    {
        const VMOp& op = ops[pc];
        unsigned int next = pc + 1;
        unsigned int taken = 0;
        unsigned int slot = sp + op.k;

        switch (op.code)
        {
            case VM_ADD:    Put(op.b, r[op.b] + r[op.a], act); break;
            case VM_SUB:    Put(op.b, r[op.b] - r[op.a], act); break;
            case VM_MUL:    Put(op.b, r[op.b] * r[op.a], act); break;
            case VM_DIV:    Put(op.b, r[op.b] / r[op.a], act); break;
            case VM_MOVE:   Put(op.b, r[op.a], act); break;
            case VM_CMP:    Compare(r[op.a], r[op.b], act); break;
            case VM_NEG:    Put(op.b, -r[op.a], act); break;
            case VM_ABS:    Put(op.b, simd::abs(r[op.a]), act); break;
            case VM_MOD:
            case VM_MODI:
                r[op.b].store(v);
                if (op.code == VM_MOD)
                    r[op.a].store(w);
                PerLane(act, v, [&](unsigned int i) { return fmod(v[i], (op.code == VM_MOD) ? w[i] : consts[op.k]); });
                r[op.b] = LaneVec::load(v);
                break;

            case VM_ADDI:   Put(op.b, r[op.b] + LaneVec(consts[op.k]), act); break;
            case VM_SUBI:   Put(op.b, r[op.b] - LaneVec(consts[op.k]), act); break;
            case VM_MULI:   Put(op.b, r[op.b] * LaneVec(consts[op.k]), act); break;
            case VM_DIVI:   Put(op.b, r[op.b] / LaneVec(consts[op.k]), act); break;
            case VM_LOADI:  Put(op.b, LaneVec(consts[op.k]), act); break;
            case VM_CMPI:   Compare(LaneVec(consts[op.k]), r[op.b], act); break;

            case VM_SEQ:    Put(op.b, Flag(eq), act); break;
            case VM_SNE:    Put(op.b, Flag(~eq), act); break;
            case VM_SLT:    Put(op.b, Flag(lt), act); break;
            case VM_SLE:    Put(op.b, Flag(lt | eq), act); break;
            case VM_SGT:    Put(op.b, Flag(~(lt | eq)), act); break;
            case VM_SGE:    Put(op.b, Flag(~lt), act); break;
            case VM_TEQ:    Put(op.b, Flag(r[op.b] == LaneVec(0.0)), act); break;
            case VM_TNE:    Put(op.b, Flag(r[op.b] != LaneVec(0.0)), act); break;

            case VM_LOADG:  Put(op.b, LaneVec(globals[op.k]), act); break;
            case VM_LOADL:
                if (slot >= ctx->maxlanestacksize)
                    return false;
                Put(op.b, LaneVec::load(ls + slot * POVFPU_LANES), act);
                break;
            case VM_STOREL:
                if (slot >= ctx->maxlanestacksize)
                    return false;
                if (act == kAllLanes)
                    r[op.b].store(ls + slot * POVFPU_LANES);
                else
                    simd::select(MaskOf(act), r[op.b], LaneVec::load(ls + slot * POVFPU_LANES)).store(ls + slot * POVFPU_LANES);
                break;

            case VM_BEQ:    taken = simd::bits(eq) & act; break;
            case VM_BNE:    taken = simd::bits(~eq) & act; break;
            case VM_BLT:    taken = simd::bits(lt) & act; break;
            case VM_BLE:    taken = simd::bits(lt | eq) & act; break;
            case VM_BGT:    taken = simd::bits(~(lt | eq)) & act; break;
            case VM_BGE:    taken = simd::bits(~lt) & act; break;
            case VM_JMP:    taken = act; break;

            case VM_XEQ:    if (simd::bits(r[op.b] == LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XNE:    if (simd::bits(r[op.b] != LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XLT:    if (simd::bits(r[op.b] < LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XLE:    if (simd::bits(r[op.b] <= LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XGT:    if (simd::bits(r[op.b] > LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XGE:    if (simd::bits(r[op.b] >= LaneVec(0.0)) & act) POVFPU_Exception(ctx, fn); break;
            case VM_XDZ:
                if (simd::bits((r[0] == LaneVec(0.0)) & (r[op.b] == LaneVec(0.0))) & act)
                    POVFPU_Exception(ctx, fn);
                break;

            case VM_RTS:
                live &= ~act;
                if (live == 0)
                    return true;
                break;
            case VM_CALLF:
                if ((slot >= ctx->maxlanestacksize) || !Frame(ops[pc + 1].k, slot, act, depth + 1))
                    return false;
                next = pc + 3;
                break;
            case VM_CALL:
                if (!Frame(op.k, sp, act, depth + 1))
                    return false;
                break;

            case VM_SQRT:   Put(0, simd::sqrt(r[0]), act); break;
            case VM_MATH1:  Put(0, Math1(op.k, r[0]), act); break;
            case VM_MATH2:  Put(0, Math2(op.k, r[0], r[1]), act); break;
            case VM_SYS1:
                r[0].store(v);
                PerLane(act, v, [&](unsigned int i) { return POVFPU_Sys1Table[op.k](v[i]); });
                r[0] = LaneVec::load(v);
                break;
            case VM_SYS2:
                r[0].store(v);
                r[1].store(w);
                PerLane(act, v, [&](unsigned int i) { return POVFPU_Sys2Table[op.k](v[i], w[i]); });
                r[0] = LaneVec::load(v);
                break;
            case VM_TRAP:
            case VM_TRAPS:
                if (!CallTrap(fn, op, sp, act, op.code == VM_TRAPS))
                    return false;
                break;

            case VM_GROW:
                if (!Reserve(slot + 1))
                    return false;
                break;
            case VM_NOP:
                break;
            default:
                return false;
        }

        const unsigned int target = op.k;
        if (!diverged)
        {
            if (taken == 0)
                pc = next;
            else if (taken == act)
                pc = target;
            else
            {
                for (unsigned int i = 0; i < POVFPU_LANES; ++i)
                    lanePc[i] = (taken & (1u << i)) ? target : next;
                diverged = true;
            }
            if (!diverged)
                continue;
        }
        else
        {
            for (unsigned int i = 0; i < POVFPU_LANES; ++i)
            {
                if (act & live & (1u << i))
                    lanePc[i] = (taken & (1u << i)) ? target : next;
            }
        }

        pc = ~0u;
        act = 0;
        for (unsigned int i = 0; i < POVFPU_LANES; ++i)
        {
            if (!(live & (1u << i)))
                continue;
            if (lanePc[i] < pc)
            {
                pc = lanePc[i];
                act = 1u << i;
            }
            else if (lanePc[i] == pc)
                act |= 1u << i;
        }
        diverged = (act != live);
    }
}

void POVFPU_RunBatch(FPUContext *context, FUNCTION fn, const DBL *x, const DBL *y, const DBL *z, DBL *results, int n)
{
    context->threaddata->Stats()[Ray_Function_VM_Calls] += (POV_ULONG)n;
    for (int base = 0; base < n; base += POVFPU_LANES)
    {
        const int count = min(POVFPU_LANES, n - base);
        BatchRun run(context, count);
        bool done = run.Reserve(INITIAL_DBL_STACK_SIZE);
        if (done)
        {
            for (int i = 0; i < POVFPU_LANES; ++i)
            {
                const int src = base + min(i, count - 1);
                run.ls[0 * POVFPU_LANES + i] = x[src];
                run.ls[1 * POVFPU_LANES + i] = y[src];
                run.ls[2 * POVFPU_LANES + i] = z[src];
            }
            done = run.Frame(fn, 0, kAllLanes, 0);
        }
        if (done)
        {
            DBL out[POVFPU_LANES];
            run.r[0].store(out);
            for (int i = 0; i < count; ++i)
                results[base + i] = out[i];
        }
        else
        {
            for (int i = 0; i < count; ++i)
            {
                context->SetLocal(0, x[base + i]);
                context->SetLocal(1, y[base + i]);
                context->SetLocal(2, z[base + i]);
                results[base + i] = POVFPU_RunScalar(context, fn);
            }
        }
    }
}

/*****************************************************************************
*
* FUNCTION
*
*   FNCode_Delete
*
* INPUT
*
*   f - function to delete
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Thorsten Froehlich
*
* DESCRIPTION
*
*   Delete a compiled function.
*
* CHANGES
*
*   -
*
******************************************************************************/

void FNCode_Delete(FunctionCode *f)
{
    int i;

    if (f->program != nullptr)
    {
        POV_FREE(f->program);
        f->program = nullptr;
    }
    for(i = 0; i < f->parameter_cnt; i++)
    {
        if (f->parameter[i] != nullptr)
        {
            POV_FREE(f->parameter[i]);
            f->parameter[i] = nullptr;
        }
    }
    for(i = 0; i < f->localvar_cnt; i++)
    {
        if (f->localvar[i] != nullptr)
        {
            POV_FREE(f->localvar[i]);
            f->localvar[i] = nullptr;
        }
    }
    if (f->private_data != nullptr)
    {
        if (f->private_destroy_method != nullptr)
            f->private_destroy_method(f->private_data);
        else
            POV_FREE(f->private_data);
        f->private_data = nullptr;
    }
}

/*****************************************************************************/

FUNCTION_PTR FunctionVM::CopyFunction(FUNCTION_PTR pK)
{
    if (pK == nullptr)
        return nullptr;

    FUNCTION_PTR ptr = new FUNCTION;

    GetFunctionAndReference(*pK); // increase the reference count
    *ptr = *pK;

    return ptr;
}

void FunctionVM::DestroyFunction(FUNCTION_PTR pK)
{
    if (pK != nullptr)
    {
        RemoveFunction(*pK);
        delete pK;
    }
}

/*****************************************************************************/

FunctionVM::CustomFunction::CustomFunction(FunctionVM* pVm, FUNCTION_PTR pFn) :
    mpVm(pVm),
    mpFn(pFn)
{}

FunctionVM::CustomFunction::~CustomFunction()
{
    mpVm->DestroyFunction(mpFn);
}

GenericFunctionContextPtr FunctionVM::CustomFunction::AcquireContext(TraceThreadData* pThreadData)
{
    FPUContext* pContext = nullptr;
    if (pThreadData->functionContextPool.empty())
        pContext = new FPUContext(mpVm.get(), pThreadData);
    else
    {
        pContext = GetFPUContextPtr(pThreadData->functionContextPool.back());
        pThreadData->functionContextPool.pop_back();
    }
    return pContext;
}

void FunctionVM::CustomFunction::ReleaseContext(GenericFunctionContextPtr pGenericContext)
{
    FPUContext* pContext = GetFPUContextPtr(pGenericContext);
    POV_VM_ASSERT (pContext->threaddata != nullptr);
    pContext->threaddata->functionContextPool.push_back (pContext);
}

void FunctionVM::CustomFunction::InitArguments(GenericFunctionContextPtr pGenericContext)
{
    FPUContext* pContext = GetFPUContextPtr(pGenericContext);
    pContext->nextArgument = 0;
}

void FunctionVM::CustomFunction::PushArgument(GenericFunctionContextPtr pGenericContext, DBL arg)
{
    FPUContext* pContext = GetFPUContextPtr(pGenericContext);
    pContext->SetLocal (pContext->nextArgument++, arg);
}

DBL FunctionVM::CustomFunction::Execute(GenericFunctionContextPtr pGenericContext)
{
    FPUContext* pContext = GetFPUContextPtr(pGenericContext);
    return POVFPU_Run (pContext, *mpFn);
}

void FunctionVM::CustomFunction::ExecuteBatch(GenericFunctionContextPtr pGenericContext, const DBL *x, const DBL *y, const DBL *z, DBL *results, int n)
{
    POVFPU_RunBatch(GetFPUContextPtr(pGenericContext), *mpFn, x, y, z, results, n);
}

GenericScalarFunctionPtr FunctionVM::CustomFunction::Clone() const
{
    return new CustomFunction(mpVm.get(), mpVm->CopyFunction(mpFn));
}

const CustomFunctionSourceInfo* FunctionVM::CustomFunction::GetSourceInfo() const
{
    return &(mpVm->GetFunction(*mpFn)->sourceInfo);
}

inline FPUContext* FunctionVM::CustomFunction::GetFPUContextPtr(GenericFunctionContextPtr pGenericContext)
{
#if POV_VM_DEBUG
    FPUContext* pContext = dynamic_cast<FPUContext*>(pGenericContext);
    POV_VM_ASSERT(pContext != nullptr);
    return pContext;
#else
    return static_cast<FPUContext*>(pGenericContext);
#endif
}

GenericFunctionContextPtr FunctionVM::CreateFunctionContext(TraceThreadData* pTd)
{
    return new FPUContext(this, pTd);
}


/*****************************************************************************/

FPUContext::FPUContext(FunctionVM* pVm, TraceThreadData* pThreadData) :
    maxdblstacksize(INITIAL_DBL_STACK_SIZE),
    dblstackbase(reinterpret_cast<DBL *>(POV_MALLOC(sizeof(DBL) * INITIAL_DBL_STACK_SIZE, "fn: dblstack"))),
    pstackbase(reinterpret_cast<StackFrame *>(POV_MALLOC(sizeof(StackFrame) * MAX_CALL_STACK_SIZE, "fn: pstack"))),
    functionvm(pVm),
    threaddata(pThreadData),
    nextArgument(0),
    lanestackbase(nullptr),
    maxlanestacksize(0)
{
    #if (SYS_FUNCTIONS == 1)
    context->dblstack = context->dblstackbase;
    #endif
}

FPUContext::~FPUContext()
{
    POV_FREE(dblstackbase);
    POV_FREE(pstackbase);
    if (lanestackbase != nullptr)
        POV_FREE(lanestackbase);
}

}
// end of namespace pov
