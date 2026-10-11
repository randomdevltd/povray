//******************************************************************************
///
/// @file vm/fnir.cpp
///
/// Lowering of decoded fncode to the register IR, and the interpreter
/// executing it: the same arithmetic, library calls and evaluation order,
/// op for op, as the fncode stack interpreter, so both produce the same bits.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "vm/fnir.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// C++ standard header files
#include <algorithm>

// POV-Ray header files (base module)
#include "base/pov_mem.h"

// POV-Ray header files (core module)
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"

// POV-Ray header files (VM module)
#include "vm/fnintern.h"
#include "vm/fnpovfpu.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::max;
using std::min;
using std::vector;

namespace
{

const unsigned int kMaxVReg = 65535;
const unsigned int kSpillHomes = 10;    // r0-r7 plus the two compare masks

// only r0's home is read before any write (a trap-only function returns through it);
// every other register the frontend lowers is written where the old VM wrote it
inline unsigned int slotBaseHomes(const FnDef* def) { return def->slotCnt - kSpillHomes; }

#ifndef FNIR_THREADED
    #if defined(__GNUC__) && !defined(__clang__)
        #define FNIR_THREADED 1
    #else
        #define FNIR_THREADED 0
    #endif
#endif

bool IsBranchCode(unsigned int code)
{
    return ((code >= VM_BEQ) && (code <= VM_BGE)) || (code == VM_JMP);
}

/// Lowers one decoded fncode program. A construct the model does not cover
/// leaves the function without an IR, and the fncode interpreter runs it.
struct Lowerer final
{
    const vector<VMOp>& src;
    const vector<DBL>& vmConsts;
    vector<FunctionEntry>& functions;
    FUNCTION self;
    unsigned int parameterCnt;

    std::shared_ptr<FnDef> def;
    unsigned int slotBase;
    unsigned int maxSlot;
    int r[8];
    int mGt, mEq;
    unsigned int sp;
    vector<int> slotVreg;
    vector<int> blockOf;
    vector<int> blockSp;
    bool ok;

    Lowerer(const vector<VMOp>& s, const vector<DBL>& c, vector<FunctionEntry>& f, FUNCTION fn) :
        src(s), vmConsts(c), functions(f), self(fn), parameterCnt(f[fn].fn.parameter_cnt),
        def(new FnDef), slotBase(0), maxSlot(0), mGt(-1), mEq(-1), sp(0), ok(true)
    {
        for (int i = 0; i < 8; ++i)
            r[i] = -1;
    }

    void Fail() { ok = false; }

    unsigned int VReg()
    {
        if (def->regCnt >= kMaxVReg)
            Fail();
        return def->regCnt++;
    }

    void Emit(unsigned int code, unsigned int a, unsigned int b, unsigned int c, unsigned int dst, unsigned int k)
    {
        if (def->ops.size() >= kMaxVReg)
        {
            Fail();
            return;
        }
        def->ops.push_back(FnOp { std::uint16_t(code), std::uint16_t(a), std::uint16_t(b),
                                  std::uint16_t(c), std::uint16_t(dst), k });
    }

    unsigned int ConstIdx(DBL v)
    {
        def->consts.push_back(v);
        return static_cast<unsigned int>(def->consts.size() - 1);
    }

    unsigned int KReg(unsigned int k)
    {
        if (k >= vmConsts.size())
        {
            Fail();
            return 0;
        }
        const unsigned int v = VReg();
        const unsigned int ki = ConstIdx(vmConsts[k]);
        Emit(FnkConst, 0, 0, 0, v, ki);
        return v;
    }

    unsigned int Def(unsigned int reg)
    {
        r[reg] = static_cast<int>(VReg());
        return static_cast<unsigned int>(r[reg]);
    }

    unsigned int Reg(unsigned int reg)
    {
        if (r[reg] < 0)
        {
            const unsigned int v = VReg();
            Emit(FnkLocalGet, 0, 0, 0, v, slotBase + reg);
            r[reg] = static_cast<int>(v);
        }
        return static_cast<unsigned int>(r[reg]);
    }

    unsigned int Mask(unsigned int m)
    {
        int held = (m == 0) ? mGt : mEq;
        if (held < 0)
        {
            const unsigned int v = VReg();
            Emit(FnkLocalGet, 0, 0, 0, v, slotBase + 8 + m);
            held = static_cast<int>(v);
        }
        if (m == 0)
            mGt = held;
        else
            mEq = held;
        return static_cast<unsigned int>(held);
    }

    unsigned int Not(unsigned int m)
    {
        const unsigned int v = VReg();
        Emit(FnkMaskNot, m, 0, 0, v, 0);
        return v;
    }

    unsigned int Both(unsigned int a, unsigned int b)
    {
        const unsigned int v = VReg();
        Emit(FnkMaskOr, a, b, 0, v, 0);
        return v;
    }

    void Flag(unsigned int m, unsigned int reg)
    {
        const unsigned int v = Def(reg);
        Emit(FnkFlag, m, 0, 0, v, 0);
    }

    void CheckSlot(unsigned int slot)
    {
        if (slot >= slotBase)
            Fail();
    }

    unsigned int SlotValue(unsigned int slot)
    {
        CheckSlot(slot);
        if (!ok)
            return 0;
        if (slotVreg[slot] < 0)
        {
            const unsigned int v = VReg();
            Emit(FnkLocalGet, 0, 0, 0, v, slot);
            slotVreg[slot] = static_cast<int>(v);
        }
        return static_cast<unsigned int>(slotVreg[slot]);
    }

    /// Copies `cnt` frame slots into consecutive fresh registers, and returns the first.
    unsigned int Gather(unsigned int firstSlot, unsigned int cnt)
    {
        const unsigned int first = def->regCnt;
        for (unsigned int j = 0; j < cnt; ++j)
            VReg();
        if (!ok)
            return 0;
        for (unsigned int j = 0; j < cnt; ++j)
        {
            const unsigned int v = SlotValue(firstSlot + j);
            Emit(FnkCopy, v, 0, 0, first + j, 0);
        }
        return first;
    }

    void Forget(unsigned int firstSlot, unsigned int cnt)
    {
        for (unsigned int j = 0; j < cnt; ++j)
        {
            CheckSlot(firstSlot + j);
            if (!ok)
                return;
            slotVreg[firstSlot + j] = -1;
        }
    }

    void Flush(unsigned int firstSlot, unsigned int cnt)
    {
        for (unsigned int j = 0; j < cnt; ++j)
        {
            const unsigned int slot = firstSlot + j;
            CheckSlot(slot);
            if (!ok)
                return;
            if (slotVreg[slot] >= 0)
            {
                const unsigned int v = static_cast<unsigned int>(slotVreg[slot]);
                Emit(FnkLocalSet, v, 0, 0, 0, slot);
                slotVreg[slot] = -1;
            }
        }
    }

    unsigned int CalleeParams(FUNCTION callee)
    {
        if ((callee >= functions.size()) || (functions[callee].fn.program == nullptr) ||
            ((callee != self) && (functions[callee].ir == nullptr)))
        {
            Fail();
            return 0;
        }
        return functions[callee].fn.parameter_cnt;
    }

    unsigned int Block(unsigned int pc)
    {
        if (pc >= blockOf.size())
        {
            Fail();
            return 0;
        }
        return static_cast<unsigned int>(blockOf[pc]);
    }

    void Note(unsigned int slotEnd)
    {
        maxSlot = max(maxSlot, slotEnd);
    }

    /// Walks a block tracking the stack offset and the highest frame slot touched.
    unsigned int Simulate(unsigned int start, unsigned int end, unsigned int spIn)
    {
        for (unsigned int pc = start; pc < end; ++pc)
        {
            const VMOp& op = src[pc];
            switch (op.code)
            {
                case VM_PUSH:
                    spIn += op.k;
                    break;
                case VM_POP:
                    if (op.k > spIn)
                    {
                        Fail();
                        return 0;
                    }
                    spIn -= op.k;
                    break;
                case VM_LOADL:
                case VM_STOREL:
                    Note(spIn + op.k + 1);
                    break;
                case VM_TRAP:
                case VM_TRAPS:
                    Note(spIn + op.a);
                    break;
                case VM_CALL:
                {
                    const unsigned int cnt = CalleeParams(op.k);
                    if (!ok)
                        return 0;
                    Note(spIn + max(cnt, (unsigned int)functions[op.k].fn.return_size));
                    break;
                }
                case VM_CALLF:
                {
                    if (pc + 2 >= src.size())
                    {
                        Fail();
                        return 0;
                    }
                    const FUNCTION callee = src[pc + 1].k;
                    const unsigned int cnt = CalleeParams(callee);
                    if (!ok)
                        return 0;
                    Note(spIn + op.k + max(cnt, (unsigned int)functions[callee].fn.return_size));
                    pc += 2;
                    break;
                }
                default:
                    break;
            }
        }
        return spIn;
    }
};

std::shared_ptr<const FnDef> LowerFunction(const vector<VMOp>& src, const vector<DBL>& vmConsts,
                                           vector<FunctionEntry>& functions, FUNCTION fn)
{
    Lowerer L(src, vmConsts, functions, fn);
    if (src.empty())
        return nullptr;
    L.slotBase = (src[0].code == VM_GROW) ? max(src[0].k + 1u, L.parameterCnt) : L.parameterCnt;

    const unsigned int n = static_cast<unsigned int>(src.size());
    vector<char> leader(n, 0);
    leader[0] = 1;
    for (unsigned int pc = 0; pc < n; ++pc)
    {
        const VMOp& op = src[pc];
        if (IsBranchCode(op.code))
        {
            if (op.k >= n)
                return nullptr;
            leader[op.k] = 1;
            if (pc + 1 < n)
                leader[pc + 1] = 1;
        }
        else if (op.code == VM_RTS)
        {
            if (pc + 1 < n)
                leader[pc + 1] = 1;
        }
        if (op.code == VM_CALLF)
            pc += 2;
    }

    vector<unsigned int> starts;
    for (unsigned int pc = 0; pc < n; ++pc)
        if (leader[pc])
            starts.push_back(pc);
    L.blockOf.assign(n, -1);
    for (unsigned int b = 0; b < starts.size(); ++b)
        for (unsigned int pc = starts[b]; pc < ((b + 1 < starts.size()) ? starts[b + 1] : n); ++pc)
            L.blockOf[pc] = static_cast<int>(b);

    // stack offsets per block, propagated over the control flow graph
    vector<int> exitSp(starts.size(), -1);
    L.blockSp.assign(starts.size(), -1);
    L.blockSp[0] = 0;
    vector<unsigned int> pending(1, 0);
    while (!pending.empty() && L.ok)
    {
        const unsigned int b = pending.back();
        pending.pop_back();
        const unsigned int start = starts[b];
        const unsigned int end = (b + 1 < starts.size()) ? starts[b + 1] : n;
        const unsigned int ex = L.Simulate(start, end, static_cast<unsigned int>(L.blockSp[b]));
        if (!L.ok)
            break;
        exitSp[b] = static_cast<int>(ex);
        const VMOp& last = src[end - 1];
        unsigned int succ[2] = { n, n };
        unsigned int succCnt = 0;
        if (last.code != VM_RTS)
        {
            if (IsBranchCode(last.code))
            {
                succ[succCnt++] = static_cast<unsigned int>(L.blockOf[last.k]);
                if (last.code != VM_JMP)
                    succ[succCnt++] = static_cast<unsigned int>(L.blockOf[end]);
            }
            else if (end < n)
                succ[succCnt++] = static_cast<unsigned int>(L.blockOf[end]);
        }
        for (unsigned int i = 0; i < succCnt; ++i)
        {
            if (L.blockSp[succ[i]] < 0)
            {
                L.blockSp[succ[i]] = static_cast<int>(ex);
                pending.push_back(succ[i]);
            }
            else if (L.blockSp[succ[i]] != static_cast<int>(ex))
                L.Fail();
        }
    }
    for (unsigned int b = 0; b < starts.size() && L.ok; ++b)
    {
        if (L.blockSp[b] < 0)
            L.blockSp[b] = (b == 0) ? 0 : exitSp[b - 1];
        const unsigned int start = starts[b];
        const unsigned int end = (b + 1 < starts.size()) ? starts[b + 1] : n;
        exitSp[b] = static_cast<int>(L.Simulate(start, end, static_cast<unsigned int>(L.blockSp[b])));
    }
    if (!L.ok)
        return nullptr;

    L.slotBase = max(L.slotBase, L.maxSlot);
    L.slotVreg.assign(L.slotBase, -1);
    L.def->parameterCnt = L.parameterCnt;
    L.def->slotCnt = L.slotBase + kSpillHomes;

    for (unsigned int b = 0; b < starts.size() && L.ok; ++b)
    {
        const unsigned int start = starts[b];
        const unsigned int end = (b + 1 < starts.size()) ? starts[b + 1] : n;
        for (int i = 0; i < 8; ++i)
            L.r[i] = -1;
        L.mGt = L.mEq = -1;
        L.sp = static_cast<unsigned int>(L.blockSp[b]);
        std::fill(L.slotVreg.begin(), L.slotVreg.end(), -1);
        L.def->blockStart.push_back(static_cast<unsigned int>(L.def->ops.size()));

        const VMOp* term = nullptr;
        for (unsigned int pc = start; (pc < end) && L.ok; ++pc)
        {
            const VMOp& op = src[pc];
            if ((op.code == VM_RTS) || IsBranchCode(op.code))
            {
                term = &op;
                break;
            }
            switch (op.code)
            {
                case VM_ADD: case VM_SUB: case VM_MUL: case VM_DIV: case VM_MOD:
                {
                    const unsigned int code = FnkAdd + (op.code - VM_ADD);
                    const unsigned int a = L.Reg(op.b);
                    const unsigned int d = L.Reg(op.a);
                    const unsigned int dst = L.Def(op.b);
                    L.Emit(code, a, d, 0, dst, 0);
                    break;
                }
                case VM_NEG: case VM_ABS:
                {
                    const unsigned int code = (op.code == VM_NEG) ? FnkNeg : FnkAbs;
                    const unsigned int a = L.Reg(op.a);
                    const unsigned int dst = L.Def(op.b);
                    L.Emit(code, a, 0, 0, dst, 0);
                    break;
                }
                case VM_MOVE:
                {
                    const unsigned int v = L.Reg(op.a);
                    L.r[op.b] = static_cast<int>(v);
                    break;
                }
                case VM_CMP:
                {
                    const unsigned int a = L.Reg(op.a);
                    const unsigned int d = L.Reg(op.b);
                    const unsigned int gt = L.VReg();
                    const unsigned int eq = L.VReg();
                    L.Emit(FnkCmpGt, a, d, 0, gt, 0);
                    L.Emit(FnkCmpEq, a, d, 0, eq, 0);
                    L.mGt = static_cast<int>(gt);
                    L.mEq = static_cast<int>(eq);
                    break;
                }
                case VM_ADDI: case VM_SUBI: case VM_MULI: case VM_DIVI: case VM_MODI:
                {
                    const unsigned int code = FnkAdd + (op.code - VM_ADDI);
                    const unsigned int a = L.Reg(op.b);
                    const unsigned int k = L.KReg(op.k);
                    const unsigned int dst = L.Def(op.b);
                    L.Emit(code, a, k, 0, dst, 0);
                    break;
                }
                case VM_LOADI:
                {
                    if (op.k >= vmConsts.size())
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int dst = L.Def(op.b);
                    L.Emit(FnkConst, 0, 0, 0, dst, L.ConstIdx(vmConsts[op.k]));
                    break;
                }
                case VM_CMPI:
                {
                    const unsigned int k = L.KReg(op.k);
                    const unsigned int d = L.Reg(op.b);
                    const unsigned int gt = L.VReg();
                    const unsigned int eq = L.VReg();
                    L.Emit(FnkCmpGt, k, d, 0, gt, 0);
                    L.Emit(FnkCmpEq, k, d, 0, eq, 0);
                    L.mGt = static_cast<int>(gt);
                    L.mEq = static_cast<int>(eq);
                    break;
                }
                case VM_SEQ: L.Flag(L.Mask(1), op.b); break;
                case VM_SNE: L.Flag(L.Not(L.Mask(1)), op.b); break;
                case VM_SLT: L.Flag(L.Mask(0), op.b); break;
                case VM_SLE: L.Flag(L.Both(L.Mask(0), L.Mask(1)), op.b); break;
                case VM_SGT: L.Flag(L.Not(L.Both(L.Mask(0), L.Mask(1))), op.b); break;
                case VM_SGE: L.Flag(L.Not(L.Mask(0)), op.b); break;
                case VM_TEQ: case VM_TNE:
                {
                    const unsigned int a = L.Reg(op.b);
                    const unsigned int z = L.VReg();
                    L.Emit(FnkConst, 0, 0, 0, z, L.ConstIdx(0.0));
                    const unsigned int m = L.VReg();
                    L.Emit(FnkCmpEq, a, z, 0, m, 0);
                    L.Flag((op.code == VM_TEQ) ? m : L.Not(m), op.b);
                    break;
                }
                case VM_LOADL:
                {
                    const unsigned int v = L.SlotValue(L.sp + op.k);
                    L.r[op.b] = static_cast<int>(v);
                    break;
                }
                case VM_STOREL:
                {
                    const unsigned int v = L.Reg(op.b);
                    L.CheckSlot(L.sp + op.k);
                    if (L.ok)
                        L.slotVreg[L.sp + op.k] = static_cast<int>(v);
                    break;
                }
                case VM_SQRT:
                {
                    const unsigned int a = L.Reg(0);
                    const unsigned int dst = L.Def(0);
                    L.Emit(FnkSys1, a, 0, 0, dst, TRAP_SYS1_SQRT);
                    break;
                }
                case VM_MATH1: case VM_SYS1:
                {
                    if (op.k >= POVFPU_Sys1TableSize)
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int a = L.Reg(0);
                    const unsigned int dst = L.Def(0);
                    L.Emit((op.code == VM_MATH1) ? FnkMath1 : FnkSys1, a, 0, 0, dst, op.k);
                    break;
                }
                case VM_MATH2: case VM_SYS2:
                {
                    if (op.k >= POVFPU_Sys2TableSize)
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int a = L.Reg(0);
                    const unsigned int b = L.Reg(1);
                    const unsigned int dst = L.Def(0);
                    L.Emit((op.code == VM_MATH2) ? FnkMath2 : FnkSys2, a, b, 0, dst, op.k);
                    break;
                }
                case VM_TRAP:
                {
                    if (op.k >= POVFPU_TrapTableSize)
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int arity = min<unsigned int>(op.a, POVFPU_TrapTable[op.k].parameter_cnt);
                    const unsigned int first = L.Gather(L.sp, arity);
                    L.Forget(L.sp, arity);
                    const unsigned int dst = L.Def(0);
                    L.Emit(FnkTrap, first, 0, 0, dst, op.k);
                    break;
                }
                case VM_TRAPS:
                {
                    if ((op.k >= POVFPU_TrapSTableSize) || (L.sp > kMaxVReg))
                    {
                        L.Fail();
                        break;
                    }
                    L.Flush(L.sp, op.a);
                    L.Forget(L.sp, op.a);
                    L.Emit(FnkTrapS, L.sp, 0, 0, 0, op.k);
                    break;
                }
                case VM_CALL:
                {
                    const unsigned int cnt = L.CalleeParams(op.k);
                    if (!L.ok)
                        break;
                    const unsigned int retSize = functions[op.k].fn.return_size;
                    if (L.sp > kMaxVReg)
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int first = L.Gather(L.sp, cnt);
                    L.Forget(L.sp, L.slotBase - L.sp);
                    const unsigned int dst = L.Def(0);
                    L.Emit(FnkCall, first, L.sp, retSize, dst, op.k);
                    break;
                }
                case VM_CALLF:
                {
                    const FUNCTION callee = src[pc + 1].k;
                    const unsigned int cnt = L.CalleeParams(callee);
                    if (!L.ok)
                        break;
                    const unsigned int retSize = functions[callee].fn.return_size;
                    const unsigned int window = L.sp + op.k;
                    if (window > kMaxVReg)
                    {
                        L.Fail();
                        break;
                    }
                    const unsigned int first = L.Gather(window, cnt);
                    L.Forget(window, L.slotBase - window);
                    const unsigned int dst = L.Def(0);
                    L.Emit(FnkCall, first, window, retSize, dst, callee);
                    pc += 2;
                    break;
                }
                case VM_PUSH:
                    L.sp += op.k;
                    break;
                case VM_POP:
                    if (op.k > L.sp)
                        L.Fail();
                    else
                        L.sp -= op.k;
                    break;
                case VM_GROW:
                    if (pc != 0)
                        L.Fail();
                    break;
                case VM_NOP:
                case VM_XEQ: case VM_XNE: case VM_XLT: case VM_XLE: case VM_XGT: case VM_XGE: case VM_XDZ:
                    break;
                default:
                    L.Fail();
                    break;
            }
        }

        unsigned int cond = 0;
        unsigned int retV = 0;
        const bool returns = (term != nullptr) && (term->code == VM_RTS);
        if (L.ok && (term != nullptr))
        {
            if (term->code == VM_RTS)
                retV = L.Reg(0);
            else if (term->code == VM_BEQ)
                cond = L.Mask(1);
            else if (term->code == VM_BNE)
                cond = L.Not(L.Mask(1));
            else if (term->code == VM_BLT)
                cond = L.Mask(0);
            else if (term->code == VM_BLE)
                cond = L.Both(L.Mask(0), L.Mask(1));
            else if (term->code == VM_BGT)
                cond = L.Not(L.Both(L.Mask(0), L.Mask(1)));
            else if (term->code == VM_BGE)
                cond = L.Not(L.Mask(0));
        }
        // a returning block has no successor to hand values to, so its live values stay put
        if (!returns)
        {
            for (unsigned int i = 0; (i < 8) && L.ok; ++i)
                if (L.r[i] >= 0)
                {
                    const unsigned int v = static_cast<unsigned int>(L.r[i]);
                    L.Emit(FnkLocalSet, v, 0, 0, 0, L.slotBase + i);
                }
            if (L.ok && (L.mGt >= 0))
            {
                const unsigned int v = static_cast<unsigned int>(L.mGt);
                L.Emit(FnkLocalSet, v, 0, 0, 0, L.slotBase + 8);
            }
            if (L.ok && (L.mEq >= 0))
            {
                const unsigned int v = static_cast<unsigned int>(L.mEq);
                L.Emit(FnkLocalSet, v, 0, 0, 0, L.slotBase + 9);
            }
            for (unsigned int s = 0; (s < L.slotBase) && L.ok; ++s)
                if (L.slotVreg[s] >= 0)
                {
                    const unsigned int v = static_cast<unsigned int>(L.slotVreg[s]);
                    L.Emit(FnkLocalSet, v, 0, 0, 0, s);
                }
        }
        if (!L.ok)
            break;

        if (term == nullptr)
            L.Emit(FnkBr, 0, 0, 0, 0, b + 1);
        else if (term->code == VM_RTS)
            L.Emit(FnkRet, retV, 0, 0, 0, 0);
        else if (term->code == VM_JMP)
            L.Emit(FnkBr, 0, 0, 0, 0, L.Block(term->k));
        else
            L.Emit(FnkBrCond, cond, L.Block(end), 0, 0, L.Block(term->k));
    }
    if (!L.ok)
        return nullptr;

    const FnDef& d = *L.def;
    const unsigned int blocks = static_cast<unsigned int>(d.blockStart.size());
    for (const FnOp& op : d.ops)
    {
        if ((op.code > FnkRet) || (op.dst >= d.regCnt))
            return nullptr;
        switch (op.code)
        {
            case FnkConst:
                if (op.k >= d.consts.size())
                    return nullptr;
                break;
            case FnkCopy: case FnkNeg: case FnkAbs: case FnkMaskNot: case FnkFlag: case FnkLocalSet:
                if (op.a >= d.regCnt)
                    return nullptr;
                break;
            case FnkAdd: case FnkSub: case FnkMul: case FnkDiv: case FnkMod:
            case FnkCmpGt: case FnkCmpEq: case FnkMaskOr:
                if ((op.a >= d.regCnt) || (op.b >= d.regCnt))
                    return nullptr;
                break;
            case FnkSelect:
                if ((op.a >= d.regCnt) || (op.b >= d.regCnt) || (op.c >= d.regCnt))
                    return nullptr;
                break;
            case FnkLocalGet:
                if (op.k >= d.slotCnt)
                    return nullptr;
                break;
            case FnkSys1: case FnkMath1:
                if ((op.k >= POVFPU_Sys1TableSize) || (op.a >= d.regCnt))
                    return nullptr;
                break;
            case FnkSys2: case FnkMath2:
                if ((op.k >= POVFPU_Sys2TableSize) || (op.a >= d.regCnt) || (op.b >= d.regCnt))
                    return nullptr;
                break;
            case FnkTrap:
                if ((op.k >= POVFPU_TrapTableSize) || (op.a >= d.regCnt) ||
                    (op.a + POVFPU_TrapTable[op.k].parameter_cnt > d.regCnt))
                    return nullptr;
                break;
            case FnkTrapS:
                if ((op.k >= POVFPU_TrapSTableSize) || (op.a >= d.slotCnt))
                    return nullptr;
                break;
            case FnkCall:
                if ((op.k >= functions.size()) || (functions[op.k].ir == nullptr) || (op.a >= d.regCnt) ||
                    (op.b >= d.slotCnt) || (op.b + op.c > d.slotCnt))
                    return nullptr;
                break;
            case FnkBr:
                if (op.k >= blocks)
                    return nullptr;
                break;
            case FnkBrCond:
                if ((op.a >= d.regCnt) || (op.k >= blocks) || (op.b >= blocks))
                    return nullptr;
                break;
            case FnkRet:
                if (op.a >= d.regCnt)
                    return nullptr;
                break;
            default:
                return nullptr;
        }
    }
    return L.def;
}

}

bool FunctionVM::InlineRound(FnDef& d)
{
    for (std::size_t idx = 0; idx < d.ops.size(); ++idx)
    {
        if (d.ops[idx].code != FnkCall)
            continue;
        const FUNCTION k = d.ops[idx].k;
        if (k >= functions.size() || functions[k].ir == nullptr)
            continue;
        const FnDef& c = *functions[k].ir;
        bool splicable = !c.ops.empty();
        for (const FnOp& co : c.ops)
            if (co.code == FnkCall || co.code == FnkTrapS)
                splicable = false;
        if (!splicable || c.ops.size() > 512 || d.ops.size() + c.ops.size() > 16384)
            continue;

        const unsigned int a0 = d.ops[idx].a;
        const unsigned int window = d.ops[idx].b;
        const unsigned int retSize = d.ops[idx].c;
        const unsigned int dst = d.ops[idx].dst;
        const unsigned int slotBase = d.slotCnt;
        const unsigned int regBase = d.regCnt;
        const unsigned int scratch = regBase + c.regCnt;
        const unsigned int constBase = d.consts.size();
        const unsigned int oldBlocks = static_cast<unsigned int>(d.blockStart.size());

        unsigned int bIdx = 0;
        for (unsigned int i = 0; i < d.blockStart.size(); ++i)
            if (idx >= d.blockStart[i])
                bIdx = i;
        const unsigned int bStart = d.blockStart[bIdx];
        const unsigned int bEnd = (bIdx + 1 < d.blockStart.size()) ? d.blockStart[bIdx + 1] : static_cast<unsigned int>(d.ops.size());

        const unsigned int calleeBlocks = static_cast<unsigned int>(c.blockStart.size());
        const unsigned int entryBlock = oldBlocks;
        const unsigned int continuationBlock = oldBlocks + calleeBlocks;
        const unsigned int scratchConst = constBase + static_cast<unsigned int>(c.consts.size());

        d.slotCnt += c.slotCnt;
        d.regCnt += c.regCnt + 1;
        d.consts.insert(d.consts.end(), c.consts.begin(), c.consts.end());
        d.consts.push_back(0.0);

        auto remap = [&](FnOp o) -> FnOp
        {
            switch (o.code)
            {
                case FnkLocalGet:
                case FnkLocalSet:
                    o.k += slotBase;
                    o.dst += regBase;
                    o.a += regBase;
                    break;
                case FnkConst:
                    o.k += constBase;
                    o.dst += regBase;
                    break;
                case FnkBr:
                    o.k += entryBlock;
                    break;
                case FnkBrCond:
                    o.a += regBase;
                    o.b += entryBlock;
                    o.k += entryBlock;
                    break;
                case FnkRet:
                    o.a += regBase;
                    break;
                case FnkTrap:
                    o.a += regBase;
                    break;
                default:
                    o.a += regBase;
                    o.b += regBase;
                    o.c += regBase;
                    o.dst += regBase;
                    break;
            }
            return o;
        };

        std::vector<FnOp> ops;
        std::vector<unsigned int> bs(oldBlocks + calleeBlocks + 1, 0);
        ops.reserve(d.ops.size() + c.ops.size() + c.parameterCnt + 2 * retSize + 8);

        for (unsigned int i = 0; i < bIdx; ++i)
            bs[i] = d.blockStart[i];
        bs[bIdx] = d.blockStart[bIdx];
        for (unsigned int i = ops.size(); i < idx; ++i)
            ops.push_back(d.ops[i]);

        for (unsigned int j = 0; j < c.parameterCnt; ++j)
            ops.push_back({FnkLocalSet, a0 + j, 0, 0, 0, slotBase + j});
        ops.push_back({FnkConst, 0, 0, 0, scratch, scratchConst});
        ops.push_back({FnkLocalSet, scratch, 0, 0, 0, slotBase + c.slotCnt - kSpillHomes});
        ops.push_back({FnkBr, 0, 0, 0, 0, entryBlock});

        for (unsigned int m = 0; m < calleeBlocks; ++m)
            bs[oldBlocks + m] = static_cast<unsigned int>(ops.size());
        for (unsigned int i = 0; i < c.ops.size(); ++i)
        {
            if (c.ops[i].code == FnkRet)
            {
                const FnOp ret = remap(c.ops[i]);
                ops.push_back({FnkCopy, ret.a, 0, 0, dst, 0});
                for (unsigned int j = 0; j < retSize; ++j)
                {
                    ops.push_back({FnkLocalGet, 0, 0, 0, scratch, slotBase + j});
                    ops.push_back({FnkLocalSet, scratch, 0, 0, 0, window + j});
                }
                ops.push_back({FnkBr, 0, 0, 0, 0, continuationBlock});
            }
            else
                ops.push_back(remap(c.ops[i]));
        }

        bs[continuationBlock] = static_cast<unsigned int>(ops.size());
        for (unsigned int i = idx + 1; i < d.ops.size(); ++i)
            ops.push_back(d.ops[i]);
        for (unsigned int i = bIdx + 1; i < d.blockStart.size(); ++i)
            bs[i] = d.blockStart[i] + (static_cast<unsigned int>(ops.size()) - bEnd);

        d.ops = std::move(ops);
        d.blockStart = std::move(bs);
        return true;
    }
    return false;
}

void FunctionVM::BuildIR(FUNCTION fn)
{
    FunctionEntry& entry = functions[fn];
    entry.ir.reset();
    if (entry.fn.program == nullptr)
        return;
    entry.ir = LowerFunction(entry.ops, consts, functions, fn);
    if (entry.ir != nullptr)
    {
        FnDef d = *entry.ir;
        for (int round = 0; round < 64; ++round)
            if (!InlineRound(d))
                break;
        entry.ir = std::make_shared<const FnDef>(std::move(d));
    }
}

#if FNIR_THREADED
__attribute__((optimize("no-crossjumping", "no-gcse", "no-tree-slp-vectorize")))
#endif
DBL FunctionVM::RunIR(FunctionVM* vm, FPUContext* ctx, FUNCTION fnIdx, const FnDef* def,
                      unsigned int frameSp, int depth, const DBL* args, unsigned int argc)
{
    if (depth >= MAX_CALL_STACK_SIZE)
    {
        POVFPU_Exception(ctx, fnIdx, "Maximum function evaluation recursion level reached.");
        return 0.0;
    }
    const unsigned int need = frameSp + def->slotCnt + def->regCnt;
    if (need > ctx->maxdblstacksize)
    {
        const unsigned int size = max(need, ctx->maxdblstacksize + (unsigned int)INITIAL_DBL_STACK_SIZE);
        ctx->dblstackbase = reinterpret_cast<DBL *>(POV_REALLOC(ctx->dblstackbase, sizeof(DBL) * size, "fn: ir frame"));
        ctx->maxdblstacksize = size;
    }
    DBL* slots = ctx->dblstackbase + frameSp;
    DBL* regs = slots + def->slotCnt;
    slots[slotBaseHomes(def)] = 0.0;
    for (unsigned int j = 0; (j < argc) && (j < def->slotCnt); ++j)
        slots[j] = args[j];
    const FnOp* const ops = def->ops.data();
    const DBL* const kConsts = def->consts.data();
    const unsigned int* const kBlocks = def->blockStart.data();
    const FnOp* op = ops;
    unsigned int pc = 0;

#define FNIR_OPS(X) \
    X(Const) X(Copy) X(Add) X(Sub) X(Mul) X(Div) X(Mod) X(Neg) X(Abs) \
    X(CmpGt) X(CmpEq) X(MaskOr) X(MaskNot) X(Flag) X(Select) \
    X(LocalGet) X(LocalSet) X(Sys1) X(Sys2) X(Math1) X(Math2) X(Trap) X(TrapS) X(Call) \
    X(Br) X(BrCond) X(Ret)

#if FNIR_THREADED
    #define FNIR_LABEL(n) &&L_##n,
    static const void* const dispatch[] = { FNIR_OPS(FNIR_LABEL) };
    #undef FNIR_LABEL
    #define FNIR_HANDLER(n) L_##n:
    #define FNIR_NEXT() { op = ops + (++pc); goto *dispatch[op->code]; }
    #define FNIR_ENTER() { op = ops + pc; goto *dispatch[op->code]; }
    goto *dispatch[op->code];
#else
    #define FNIR_HANDLER(n) case Fnk##n:
    #define FNIR_NEXT() { ++pc; continue; }
    #define FNIR_ENTER() { op = ops + pc; continue; }
    for (;;)
    {
        op = ops + pc;
        switch (op->code)
        {
#endif

    FNIR_HANDLER(Const)
        regs[op->dst] = kConsts[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(Copy)
        regs[op->dst] = regs[op->a];
        FNIR_NEXT();
    FNIR_HANDLER(Add)
        regs[op->dst] = regs[op->a] + regs[op->b];
        FNIR_NEXT();
    FNIR_HANDLER(Sub)
        regs[op->dst] = regs[op->a] - regs[op->b];
        FNIR_NEXT();
    FNIR_HANDLER(Mul)
        regs[op->dst] = regs[op->a] * regs[op->b];
        FNIR_NEXT();
    FNIR_HANDLER(Div)
        regs[op->dst] = regs[op->a] / regs[op->b];
        FNIR_NEXT();
    FNIR_HANDLER(Mod)
        regs[op->dst] = fmod(regs[op->a], regs[op->b]);
        FNIR_NEXT();
    FNIR_HANDLER(Neg)
        regs[op->dst] = -regs[op->a];
        FNIR_NEXT();
    FNIR_HANDLER(Abs)
        regs[op->dst] = fabs(regs[op->a]);
        FNIR_NEXT();
    FNIR_HANDLER(CmpGt)
        regs[op->dst] = (regs[op->a] > regs[op->b]) ? 1.0 : 0.0;
        FNIR_NEXT();
    FNIR_HANDLER(CmpEq)
        regs[op->dst] = (regs[op->a] == regs[op->b]) ? 1.0 : 0.0;
        FNIR_NEXT();
    FNIR_HANDLER(MaskOr)
        regs[op->dst] = ((regs[op->a] != 0.0) || (regs[op->b] != 0.0)) ? 1.0 : 0.0;
        FNIR_NEXT();
    FNIR_HANDLER(MaskNot)
        regs[op->dst] = (regs[op->a] != 0.0) ? 0.0 : 1.0;
        FNIR_NEXT();
    FNIR_HANDLER(Flag)
        regs[op->dst] = regs[op->a];
        FNIR_NEXT();
    FNIR_HANDLER(Select)
        regs[op->dst] = (regs[op->a] != 0.0) ? regs[op->b] : regs[op->c];
        FNIR_NEXT();
    FNIR_HANDLER(LocalGet)
        regs[op->dst] = slots[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(LocalSet)
        slots[op->k] = regs[op->a];
        FNIR_NEXT();
    FNIR_HANDLER(Sys1)
        regs[op->dst] = POVFPU_Sys1Table[op->k](regs[op->a]);
        FNIR_NEXT();
    FNIR_HANDLER(Sys2)
        regs[op->dst] = POVFPU_Sys2Table[op->k](regs[op->a], regs[op->b]);
        FNIR_NEXT();
    FNIR_HANDLER(Math1)
        regs[op->dst] = POVFPU_Math1Scalar(op->k, regs[op->a]);
        FNIR_NEXT();
    FNIR_HANDLER(Math2)
        regs[op->dst] = POVFPU_Math2Scalar(op->k, regs[op->a], regs[op->b]);
        FNIR_NEXT();
    FNIR_HANDLER(Trap)
    {
        DBL* const args = regs + op->a;
        const DBL result = POVFPU_TrapTable[op->k].fn(ctx, args, fnIdx);
        slots = ctx->dblstackbase + frameSp;
        regs = slots + def->slotCnt;
        regs[op->dst] = result;
        FNIR_NEXT();
    }
    FNIR_HANDLER(TrapS)
    {
        POVFPU_TrapSTable[op->k].fn(ctx, slots + op->a, fnIdx, frameSp + op->a);
        slots = ctx->dblstackbase + frameSp;
        regs = slots + def->slotCnt;
        FNIR_NEXT();
    }
    FNIR_HANDLER(Call)
    {
        const FnDef* const callee = vm->functions[op->k].ir.get();
        const unsigned int cnt = callee->parameterCnt;
        DBL argv[MAX_FUNCTION_PARAMETER_LIST];
        for (unsigned int j = 0; j < cnt; ++j)
            argv[j] = regs[op->a + j];
        const unsigned int calleeSp = frameSp + def->slotCnt + def->regCnt;
        const DBL result = RunIR(vm, ctx, op->k, callee, calleeSp, depth + 1, argv, cnt);
        DBL* const calleeSlots = ctx->dblstackbase + calleeSp;
        slots = ctx->dblstackbase + frameSp;
        for (unsigned int j = 0; j < op->c; ++j)
            slots[op->b + j] = calleeSlots[j];
        regs = slots + def->slotCnt;
        regs[op->dst] = result;
        FNIR_NEXT();
    }
    FNIR_HANDLER(Br)
        pc = kBlocks[op->k];
        FNIR_ENTER();
    FNIR_HANDLER(BrCond)
        if (regs[op->a] != 0.0)
            pc = kBlocks[op->k];
        else
            pc = kBlocks[op->b];
        FNIR_ENTER();
    FNIR_HANDLER(Ret)
        return regs[op->a];

#if !FNIR_THREADED
        }
    }
#endif
#undef FNIR_HANDLER
#undef FNIR_NEXT
#undef FNIR_ENTER
#undef FNIR_OPS
}

DBL POVFPU_RunIR(FPUContext* context, FUNCTION fn)
{
    context->threaddata->Stats()[Ray_Function_VM_Calls]++;
    const FnDef* def = context->functionvm->GetIR(fn);
    if (def == nullptr)
        return POVFPU_RunScalar(context, fn);
    return FunctionVM::RunIR(context->functionvm.get(), context, fn, def, 0, 0, nullptr, 0);
}

void POVFPU_RunBatchIR(FPUContext* context, FUNCTION fn, const DBL* x, const DBL* y, const DBL* z,
                       DBL* results, int n)
{
    const FnDef* def = context->functionvm->GetIR(fn);
    if (def == nullptr)
    {
        POVFPU_RunBatch(context, fn, x, y, z, results, n);
        return;
    }
    context->threaddata->Stats()[Ray_Function_VM_Calls] += static_cast<POV_ULONG>(n);
    FunctionVM* const vm = context->functionvm.get();
    for (int i = 0; i < n; ++i)
    {
        context->SetLocal(0, x[i]);
        context->SetLocal(1, y[i]);
        context->SetLocal(2, z[i]);
        results[i] = FunctionVM::RunIR(vm, context, fn, def, 0, 0, nullptr, 0);
    }
}

}
// end of namespace pov
