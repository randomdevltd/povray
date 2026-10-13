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

    unsigned int Imm(unsigned int k)
    {
        if (k >= vmConsts.size())
        {
            Fail();
            return 0;
        }
        return ConstIdx(vmConsts[k]);
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
                    Note(spIn + cnt + functions[op.k].fn.return_size);
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
                    Note(spIn + op.k + cnt + functions[callee].fn.return_size);
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

enum FieldRole
{
    RoleA = 1, RoleB = 2, RoleC = 4, RoleDst = 8,
    RoleKConst = 16, RoleKSlot = 32, RoleKBlock = 64, RoleBBlock = 128, RoleASlot = 256, RoleBSlot = 512
};

/// What each operand field of an op names; the validator and the inliner both read it.
unsigned int FieldRoles(unsigned int code)
{
    switch (code)
    {
        case FnkConst:                                                  return RoleDst | RoleKConst;
        case FnkCopy: case FnkNeg: case FnkAbs: case FnkMaskNot: case FnkFlag:
        case FnkSys1: case FnkMath1: case FnkTrap:                      return RoleA | RoleDst;
        case FnkAdd: case FnkSub: case FnkMul: case FnkDiv: case FnkMod:
        case FnkCmpGt: case FnkCmpEq: case FnkMaskOr:
        case FnkSys2: case FnkMath2:                                    return RoleA | RoleB | RoleDst;
        case FnkSelect:                                                 return RoleA | RoleB | RoleC | RoleDst;
        case FnkAddK: case FnkSubK: case FnkMulK: case FnkDivK: case FnkModK:
        case FnkCmpGtKA: case FnkCmpEqKA:                               return RoleA | RoleDst | RoleKConst;
        case FnkLocalGet:                                               return RoleDst | RoleKSlot;
        case FnkLocalSet:                                               return RoleA | RoleKSlot;
        case FnkTrapS:                                                  return RoleASlot;
        case FnkCall:                                                   return RoleA | RoleDst | RoleBSlot;
        case FnkBr:                                                     return RoleKBlock;
        case FnkBrCond:                                                 return RoleA | RoleKBlock | RoleBBlock;
        case FnkRet:                                                    return RoleA;
        default:                                                        return 0;
    }
}

bool IsTerminator(unsigned int code)
{
    return (code == FnkBr) || (code == FnkBrCond) || (code == FnkRet);
}

/// Every operand in range, every block closed by one terminator or falling through into the next,
/// the last op a terminator; a def failing it never runs.
bool ValidDef(const FnDef& d, const vector<FunctionEntry>& functions)
{
    const std::size_t n = d.ops.size();
    if ((n == 0) || d.blockStart.empty() || (d.regCnt > kMaxVReg + 1) || (d.slotCnt < kSpillHomes) ||
        (d.argBase + d.parameterCnt > d.slotCnt - kSpillHomes))
        return false;
    vector<unsigned int> starts(d.blockStart);
    std::sort(starts.begin(), starts.end());
    if (starts[0] != 0)
        return false;
    vector<char> isStart(n + 1, 0);
    for (std::size_t i = 0; i < starts.size(); ++i)
    {
        if ((starts[i] >= n) || ((i > 0) && (starts[i] == starts[i - 1])))
            return false;
        isStart[starts[i]] = 1;
    }
    isStart[n] = 1;
    if (!IsTerminator(d.ops[n - 1].code))
        return false;
    const std::size_t blocks = d.blockStart.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const FnOp& op = d.ops[i];
        if ((op.code > FnkRet) || (IsTerminator(op.code) && !isStart[i + 1]))
            return false;
        const unsigned int roles = FieldRoles(op.code);
        if (((roles & RoleA) && (op.a >= d.regCnt)) || ((roles & RoleB) && (op.b >= d.regCnt)) ||
            ((roles & RoleC) && (op.c >= d.regCnt)) || ((roles & RoleDst) && (op.dst >= d.regCnt)) ||
            ((roles & RoleKConst) && (op.k >= d.consts.size())) || ((roles & RoleKSlot) && (op.k >= d.slotCnt)) ||
            ((roles & RoleKBlock) && (op.k >= blocks)) || ((roles & RoleBBlock) && (op.b >= blocks)) ||
            ((roles & RoleASlot) && (op.a >= d.slotCnt)))
            return false;
        switch (op.code)
        {
            case FnkSys1: case FnkMath1:
                if (op.k >= POVFPU_Sys1TableSize)
                    return false;
                break;
            case FnkSys2: case FnkMath2:
                if (op.k >= POVFPU_Sys2TableSize)
                    return false;
                break;
            case FnkTrap:
                if ((op.k >= POVFPU_TrapTableSize) || (op.a + POVFPU_TrapTable[op.k].parameter_cnt > d.regCnt) ||
                    ((op.b | (unsigned int)(op.c) << 16) >= functions.size()))
                    return false;
                break;
            case FnkTrapS:
                if (op.k >= POVFPU_TrapSTableSize)
                    return false;
                break;
            case FnkCall:
            {
                if ((op.k >= functions.size()) || (functions[op.k].ir == nullptr))
                    return false;
                const unsigned int cnt = functions[op.k].ir->parameterCnt;
                if ((cnt > MAX_FUNCTION_PARAMETER_LIST) || (op.a + cnt > d.regCnt) || (op.b + op.c > d.slotCnt))
                    return false;
                break;
            }
            default:
                break;
        }
    }
    return true;
}

FnOp MakeOp(unsigned int code, unsigned int a, unsigned int b, unsigned int c, unsigned int dst, unsigned int k)
{
    return FnOp { std::uint16_t(code), std::uint16_t(a), std::uint16_t(b), std::uint16_t(c), std::uint16_t(dst), k };
}

void Compact(vector<FnOp>& ops, const vector<char>& keep)
{
    std::size_t j = 0;
    for (std::size_t i = 0; i < ops.size(); ++i)
        if (keep[i])
            ops[j++] = ops[i];
    ops.resize(j);
}

/// Number of consecutive registers from `a` a trap or call reads.
unsigned int ArgCount(const FnOp& op, const vector<FunctionEntry>& functions)
{
    if (op.code == FnkTrap)
        return POVFPU_TrapTable[op.k].parameter_cnt;
    if (op.code == FnkCall)
        return functions[op.k].ir->parameterCnt;
    return 0;
}

template <typename F>
void ForEachRead(const FnOp& op, const vector<FunctionEntry>& functions, F f)
{
    if ((op.code == FnkTrap) || (op.code == FnkCall))
    {
        const unsigned int cnt = ArgCount(op, functions);
        for (unsigned int j = 0; j < cnt; ++j)
            f(op.a + j);
        return;
    }
    const unsigned int roles = FieldRoles(op.code);
    if (roles & RoleA)
        f(op.a);
    if (roles & RoleB)
        f(op.b);
    if (roles & RoleC)
        f(op.c);
}

/// The registers an op writes: dst, and for a trap its argument range, which traps overwrite in place.
template <typename F>
void ForEachWrite(const FnOp& op, const vector<FunctionEntry>& functions, F f)
{
    if (FieldRoles(op.code) & RoleDst)
        f(op.dst);
    if (op.code == FnkTrap)
    {
        const unsigned int cnt = ArgCount(op, functions);
        for (unsigned int j = 0; j < cnt; ++j)
            f(op.a + j);
    }
}

/// No effect besides writing dst, so it may be dropped when nothing reads dst.
bool IsPure(unsigned int code)
{
    return (FieldRoles(code) & RoleDst) && (code != FnkTrap) && (code != FnkCall);
}

/// A def as a control flow graph: blocks in physical order, the entry first, each a body
/// and one terminator whose targets are block indices.
struct CfgBlock final
{
    vector<FnOp> body;
    FnOp term;
    bool live;
};

vector<CfgBlock> Decode(const FnDef& d)
{
    const std::size_t n = d.ops.size();
    vector<unsigned int> starts(d.blockStart);
    std::sort(starts.begin(), starts.end());
    vector<unsigned int> indexAt(n, 0);
    for (unsigned int p = 0; p < starts.size(); ++p)
        indexAt[starts[p]] = p;
    vector<CfgBlock> blocks(starts.size());
    for (unsigned int p = 0; p < starts.size(); ++p)
    {
        const std::size_t end = (p + 1 < starts.size()) ? starts[p + 1] : n;
        CfgBlock& b = blocks[p];
        b.live = true;
        b.body.assign(d.ops.begin() + starts[p], d.ops.begin() + end);
        b.term = b.body.back();
        if (!IsTerminator(b.term.code))
        {
            b.term = MakeOp(FnkBr, 0, 0, 0, 0, p + 1);
            continue;
        }
        b.body.pop_back();
        if (b.term.code == FnkBr)
            b.term.k = indexAt[d.blockStart[b.term.k]];
        else if (b.term.code == FnkBrCond)
        {
            b.term.k = indexAt[d.blockStart[b.term.k]];
            b.term.b = std::uint16_t(indexAt[d.blockStart[b.term.b]]);
        }
    }
    return blocks;
}

/// Writes the live blocks back in order; a branch to the block that follows becomes a fallthrough.
void Encode(const vector<CfgBlock>& blocks, FnDef& d)
{
    vector<unsigned int> id(blocks.size(), 0);
    vector<unsigned int> next(blocks.size(), 0);
    unsigned int cnt = 0;
    for (unsigned int p = 0; p < blocks.size(); ++p)
        if (blocks[p].live)
            id[p] = cnt++;
    for (unsigned int p = static_cast<unsigned int>(blocks.size()), following = ~0u; p-- > 0;)
    {
        next[p] = following;
        if (blocks[p].live)
            following = p;
    }
    vector<FnOp> ops;
    vector<unsigned int> starts;
    for (unsigned int p = 0; p < blocks.size(); ++p)
    {
        const CfgBlock& b = blocks[p];
        if (!b.live)
            continue;
        starts.push_back(static_cast<unsigned int>(ops.size()));
        ops.insert(ops.end(), b.body.begin(), b.body.end());
        FnOp t = b.term;
        if ((t.code == FnkBr) && (t.k == next[p]) && !b.body.empty())
            continue;
        if (t.code == FnkBr)
            t.k = id[t.k];
        else if (t.code == FnkBrCond)
        {
            t.k = id[t.k];
            t.b = std::uint16_t(id[t.b]);
        }
        ops.push_back(t);
    }
    d.ops = std::move(ops);
    d.blockStart = std::move(starts);
}

template <typename F>
void ForEachSuccessor(const FnOp& term, F f)
{
    if (term.code == FnkBr)
        f(term.k);
    else if (term.code == FnkBrCond)
    {
        f(term.k);
        f(term.b);
    }
}

vector<unsigned int> Predecessors(const vector<CfgBlock>& blocks)
{
    vector<unsigned int> preds(blocks.size(), 0);
    preds[0] = 1;
    for (const CfgBlock& b : blocks)
        if (b.live)
            ForEachSuccessor(b.term, [&](unsigned int s) { ++preds[s]; });
    return preds;
}

bool RemoveUnreachable(vector<CfgBlock>& blocks)
{
    vector<char> seen(blocks.size(), 0);
    vector<unsigned int> work(1, 0);
    seen[0] = 1;
    while (!work.empty())
    {
        const unsigned int p = work.back();
        work.pop_back();
        ForEachSuccessor(blocks[p].term, [&](unsigned int s) { if (!seen[s]) { seen[s] = 1; work.push_back(s); } });
    }
    bool changed = false;
    for (unsigned int p = 0; p < blocks.size(); ++p)
        if (blocks[p].live && !seen[p])
        {
            blocks[p].live = false;
            changed = true;
        }
    return changed;
}

/// Branches to an empty block go straight to where it leads, and a block entered only by a
/// branch from one other block is appended to that block.
bool Straighten(vector<CfgBlock>& blocks)
{
    const unsigned int cnt = static_cast<unsigned int>(blocks.size());
    auto through = [&](unsigned int s)
    {
        for (unsigned int steps = 0; (steps < cnt) && (s != 0) && blocks[s].body.empty() && (blocks[s].term.code == FnkBr) &&
                                     (blocks[s].term.k != s); ++steps)
            s = blocks[s].term.k;
        return s;
    };
    for (CfgBlock& b : blocks)
    {
        if (!b.live)
            continue;
        FnOp& t = b.term;
        if (t.code == FnkBr)
            t.k = through(t.k);
        else if (t.code == FnkBrCond)
        {
            t.k = through(t.k);
            t.b = std::uint16_t(through(t.b));
            if (t.k == t.b)
                t = MakeOp(FnkBr, 0, 0, 0, 0, t.k);
        }
    }
    bool changed = RemoveUnreachable(blocks);
    vector<unsigned int> preds = Predecessors(blocks);
    for (unsigned int p = 0; p < cnt; ++p)
    {
        CfgBlock& b = blocks[p];
        while (b.live && (b.term.code == FnkBr) && (b.term.k != p) && (b.term.k != 0) && (preds[b.term.k] == 1))
        {
            CfgBlock& s = blocks[b.term.k];
            b.body.insert(b.body.end(), s.body.begin(), s.body.end());
            b.term = s.term;
            s.live = false;
            s.body.clear();
            changed = true;
        }
    }
    return changed;
}

/// Within a block: loads of a slot a register holds become copies, stores of the value a slot holds
/// go, reads of a copy read its source, and branches and selects on a negated mask swap their arms.
bool Forward(vector<CfgBlock>& blocks, const FnDef& d, const vector<FunctionEntry>& functions)
{
    struct Alias { unsigned int src, srcVer, ver, stamp; };
    struct Known { unsigned int reg, regVer, slotVer, stamp; };
    vector<unsigned int> ver(d.regCnt, 0);
    vector<unsigned int> slotVer(d.slotCnt, 0);
    vector<Alias> alias(d.regCnt, Alias { 0, 0, 0, 0 });
    vector<Alias> negation(d.regCnt, Alias { 0, 0, 0, 0 });
    vector<Known> known(d.slotCnt, Known { 0, 0, 0, 0 });
    unsigned int stamp = 0;
    bool changed = false;
    for (CfgBlock& b : blocks)
    {
        if (!b.live)
            continue;
        const unsigned int blockStamp = ++stamp;
        unsigned int clearStamp = blockStamp;
        auto current = [&](const vector<Alias>& v, unsigned int r)
        {
            const Alias& x = v[r];
            return (x.stamp > blockStamp) && (ver[r] == x.ver) && (ver[x.src] == x.srcVer);
        };
        auto root = [&](unsigned int r) { return current(alias, r) ? alias[r].src : r; };
        auto holder = [&](unsigned int s) -> int
        {
            const Known& x = known[s];
            if ((x.stamp > clearStamp) && (slotVer[s] == x.slotVer) && (ver[x.reg] == x.regVer))
                return static_cast<int>(x.reg);
            return -1;
        };
        vector<FnOp> body;
        body.reserve(b.body.size());
        for (std::size_t i = 0; i <= b.body.size(); ++i)
        {
            FnOp op = (i < b.body.size()) ? b.body[i] : b.term;
            const unsigned int roles = FieldRoles(op.code);
            if ((op.code != FnkTrap) && (op.code != FnkCall))
            {
                const FnOp was = op;
                if (roles & RoleA) op.a = std::uint16_t(root(op.a));
                if (roles & RoleB) op.b = std::uint16_t(root(op.b));
                if (roles & RoleC) op.c = std::uint16_t(root(op.c));
                if (((op.code == FnkBrCond) || (op.code == FnkSelect)) && current(negation, op.a))
                {
                    op.a = std::uint16_t(negation[op.a].src);
                    if (op.code == FnkBrCond)
                    {
                        const unsigned int taken = op.k;
                        op.k = op.b;
                        op.b = std::uint16_t(taken);
                    }
                    else
                        std::swap(op.b, op.c);
                }
                changed = changed || (op.a != was.a) || (op.b != was.b) || (op.c != was.c) || (op.k != was.k);
            }
            if (op.code == FnkLocalGet)
            {
                const int h = holder(op.k);
                if (h >= 0)
                {
                    op = MakeOp(FnkCopy, static_cast<unsigned int>(h), 0, 0, op.dst, 0);
                    changed = true;
                }
            }
            else if (op.code == FnkLocalSet)
            {
                if (holder(op.k) == static_cast<int>(op.a))
                {
                    changed = true;
                    continue;
                }
                slotVer[op.k] = ++stamp;
                known[op.k] = Known { op.a, ver[op.a], slotVer[op.k], ++stamp };
            }
            else if ((op.code == FnkTrapS) || (op.code == FnkCall))
                clearStamp = ++stamp;
            ForEachWrite(op, functions, [&](unsigned int r) { ver[r] = ++stamp; });
            if (FieldRoles(op.code) & RoleDst)
            {
                if ((op.code == FnkCopy) && (op.a != op.dst))
                    alias[op.dst] = Alias { op.a, ver[op.a], ver[op.dst], ++stamp };
                else if ((op.code == FnkMaskNot) && (op.a != op.dst))
                    negation[op.dst] = Alias { op.a, ver[op.a], ver[op.dst], ++stamp };
                else if (op.code == FnkLocalGet)
                    known[op.k] = Known { op.dst, ver[op.dst], slotVer[op.k], ++stamp };
            }
            if (i < b.body.size())
                body.push_back(op);
            else
                b.term = op;
        }
        b.body = std::move(body);
    }
    return changed;
}

/// Drops pure ops whose result nothing reads, and copies of a register onto itself.
bool RemoveDeadOps(vector<CfgBlock>& blocks, const FnDef& d, const vector<FunctionEntry>& functions)
{
    vector<unsigned int> uses(d.regCnt, 0);
    for (const CfgBlock& b : blocks)
    {
        if (!b.live)
            continue;
        for (const FnOp& op : b.body)
            ForEachRead(op, functions, [&](unsigned int r) { ++uses[r]; });
        ForEachRead(b.term, functions, [&](unsigned int r) { ++uses[r]; });
    }
    bool changed = false;
    for (bool again = true; again;)
    {
        again = false;
        for (CfgBlock& b : blocks)
        {
            if (!b.live)
                continue;
            vector<char> keep(b.body.size(), 1);
            for (std::size_t i = b.body.size(); i-- > 0;)
            {
                const FnOp& op = b.body[i];
                const bool selfCopy = (op.code == FnkCopy) && (op.a == op.dst);
                if (!selfCopy && !(IsPure(op.code) && (uses[op.dst] == 0)))
                    continue;
                ForEachRead(op, functions, [&](unsigned int r) { --uses[r]; });
                keep[i] = 0;
                again = changed = true;
            }
            Compact(b.body, keep);
        }
    }
    return changed;
}

/// Backward slot liveness over the graph; a store to a slot no path reads before the next store goes.
bool RemoveDeadStores(vector<CfgBlock>& blocks, const FnDef& d)
{
    const std::size_t words = (d.slotCnt + 63) / 64;
    const std::size_t cnt = blocks.size();
    if (words * cnt > (std::size_t(1) << 22))
        return false;
    typedef vector<std::uint64_t> Bits;
    auto set = [](Bits& v, unsigned int s) { v[s >> 6] |= std::uint64_t(1) << (s & 63); };
    auto reset = [](Bits& v, unsigned int s) { v[s >> 6] &= ~(std::uint64_t(1) << (s & 63)); };
    auto test = [](const Bits& v, unsigned int s) { return ((v[s >> 6] >> (s & 63)) & 1) != 0; };
    // a returning block hands the result area of a vector function to its caller
    Bits atReturn(words, 0);
    for (unsigned int s = 0; s < d.argBase; ++s)
        set(atReturn, s);

    // the slots live on entry to `b` given those live at its end; with `keep`, marks the dead stores
    auto transfer = [&](const CfgBlock& b, Bits live, vector<char>* keep)
    {
        for (std::size_t i = b.body.size(); i-- > 0;)
        {
            const FnOp& op = b.body[i];
            if (op.code == FnkLocalGet)
                set(live, op.k);
            else if (op.code == FnkLocalSet)
            {
                if ((keep != nullptr) && !test(live, op.k))
                    (*keep)[i] = 0;
                else
                    reset(live, op.k);
            }
            else if (op.code == FnkCall)
            {
                for (unsigned int j = 0; j < op.c; ++j)
                    reset(live, op.b + j);
            }
            else if (op.code == FnkTrapS)
                std::fill(live.begin(), live.end(), ~std::uint64_t(0));
        }
        return live;
    };
    auto liveOut = [&](const vector<Bits>& liveIn, const CfgBlock& b)
    {
        Bits out(words, 0);
        if (b.term.code == FnkRet)
            out = atReturn;
        ForEachSuccessor(b.term, [&](unsigned int s) { for (std::size_t w = 0; w < words; ++w) out[w] |= liveIn[s][w]; });
        return out;
    };

    vector<Bits> liveIn(cnt, Bits(words, 0));
    bool stable = false;
    for (std::size_t round = 0; (round < cnt + 2) && !stable; ++round)
    {
        stable = true;
        for (std::size_t p = cnt; p-- > 0;)
        {
            if (!blocks[p].live)
                continue;
            Bits in = transfer(blocks[p], liveOut(liveIn, blocks[p]), nullptr);
            if (in != liveIn[p])
            {
                liveIn[p] = std::move(in);
                stable = false;
            }
        }
    }
    if (!stable)
        return false;
    bool changed = false;
    for (std::size_t p = 0; p < cnt; ++p)
    {
        if (!blocks[p].live)
            continue;
        vector<char> keep(blocks[p].body.size(), 1);
        transfer(blocks[p], liveOut(liveIn, blocks[p]), &keep);
        if (std::find(keep.begin(), keep.end(), 0) == keep.end())
            continue;
        Compact(blocks[p].body, keep);
        changed = true;
    }
    return changed;
}

/// A register read only by a later copy in the same block is computed straight into the copy's
/// destination, so argument ranges fill without the copy.
bool Retarget(vector<CfgBlock>& blocks, const FnDef& d, const vector<FunctionEntry>& functions)
{
    vector<unsigned int> uses(d.regCnt, 0);
    for (const CfgBlock& b : blocks)
    {
        if (!b.live)
            continue;
        for (const FnOp& op : b.body)
            ForEachRead(op, functions, [&](unsigned int r) { ++uses[r]; });
        ForEachRead(b.term, functions, [&](unsigned int r) { ++uses[r]; });
    }
    bool changed = false;
    for (CfgBlock& b : blocks)
    {
        if (!b.live)
            continue;
        vector<char> keep(b.body.size(), 1);
        for (std::size_t i = 0; i < b.body.size(); ++i)
        {
            const FnOp copy = b.body[i];
            if ((copy.code != FnkCopy) || (uses[copy.a] != 1) || (copy.a == copy.dst))
                continue;
            for (std::size_t j = i; (j-- > 0) && (i - j <= 64);)
            {
                if (!keep[j])
                    continue;
                FnOp& q = b.body[j];
                bool touches = false;
                ForEachRead(q, functions, [&](unsigned int r) { touches = touches || (r == copy.dst); });
                ForEachWrite(q, functions, [&](unsigned int r) { touches = touches || (r == copy.dst); });
                if (touches)
                    break;
                if ((FieldRoles(q.code) & RoleDst) && (q.dst == copy.a))
                {
                    q.dst = copy.dst;
                    keep[i] = 0;
                    uses[copy.a] = 0;
                    changed = true;
                    break;
                }
            }
        }
        Compact(b.body, keep);
    }
    return changed;
}

const unsigned int kSpeculateOps = 8;

/// Ops an arm may run unconditionally: cheap, no library call, no effect beyond its registers.
bool IsSpeculable(unsigned int code)
{
    switch (code)
    {
        case FnkConst: case FnkCopy: case FnkAdd: case FnkSub: case FnkMul: case FnkDiv: case FnkNeg: case FnkAbs:
        case FnkCmpGt: case FnkCmpEq: case FnkMaskOr: case FnkMaskNot: case FnkFlag: case FnkSelect:
        case FnkAddK: case FnkSubK: case FnkMulK: case FnkDivK: case FnkCmpGtKA: case FnkCmpEqKA:
        case FnkLocalGet: case FnkLocalSet:
            return true;
        default:
            return false;
    }
}

/// One pass turning branch diamonds and triangles with short arms into straight code: both arms
/// run, and each slot an arm stores is set by a select on the branch mask, as the branch would.
bool SpeculateRound(vector<CfgBlock>& blocks, FnDef& d, const vector<FunctionEntry>& functions)
{
    const int kNone = -1, kSeveral = -2;
    vector<unsigned int> preds;
    vector<int> defIn, readIn;
    auto survey = [&]()
    {
        preds = Predecessors(blocks);
        defIn.assign(d.regCnt, kNone);
        readIn.assign(d.regCnt, kNone);
        auto note = [&](vector<int>& v, unsigned int r, int p) { v[r] = (v[r] == kNone || v[r] == p) ? p : kSeveral; };
        for (unsigned int p = 0; p < blocks.size(); ++p)
        {
            if (!blocks[p].live)
                continue;
            for (const FnOp& op : blocks[p].body)
            {
                ForEachRead(op, functions, [&](unsigned int r) { note(readIn, r, int(p)); });
                ForEachWrite(op, functions, [&](unsigned int r) { note(defIn, r, int(p)); });
            }
            ForEachRead(blocks[p].term, functions, [&](unsigned int r) { note(readIn, r, int(p)); });
        }
    };
    // an arm is entered only from the branch, leads straight to the join and keeps its registers to itself
    auto isArm = [&](unsigned int x, unsigned int branch, unsigned int join)
    {
        const CfgBlock& a = blocks[x];
        if ((x == 0) || (x == branch) || (x == join) || (preds[x] != 1) || (a.term.code != FnkBr) || (a.term.k != join))
            return false;
        unsigned int ops = 0;
        vector<unsigned int> stored;
        for (const FnOp& op : a.body)
        {
            if (!IsSpeculable(op.code))
                return false;
            if (op.code == FnkLocalSet)
            {
                stored.push_back(op.k);
                continue;
            }
            if ((op.code == FnkLocalGet) && (std::find(stored.begin(), stored.end(), op.k) != stored.end()))
                return false;
            if ((defIn[op.dst] != int(x)) || ((readIn[op.dst] != kNone) && (readIn[op.dst] != int(x))))
                return false;
            ++ops;
        }
        return ops <= kSpeculateOps;
    };

    survey();
    bool changed = false;
    for (unsigned int p = 0; p < blocks.size(); ++p)
    {
        CfgBlock& b = blocks[p];
        if (!b.live || (b.term.code != FnkBrCond))
            continue;
        const unsigned int t = b.term.k, f = b.term.b;
        int armT = -1, armF = -1;
        unsigned int join = 0;
        if (isArm(t, p, f))
        {
            armT = int(t);
            join = f;
        }
        else if (isArm(f, p, t))
        {
            armF = int(f);
            join = t;
        }
        else if ((blocks[t].term.code == FnkBr) && (blocks[f].term.code == FnkBr) && (blocks[t].term.k == blocks[f].term.k) &&
                 isArm(t, p, blocks[t].term.k) && isArm(f, p, blocks[t].term.k))
        {
            armT = int(t);
            armF = int(f);
            join = blocks[t].term.k;
        }
        else
            continue;
        if (join == p)
            continue;

        // the value each arm leaves in each slot it stores, in first-store order
        vector<unsigned int> slots;
        vector<int> valueT, valueF;
        vector<FnOp> body(b.body);
        for (int side = 0; side < 2; ++side)
        {
            const int arm = side ? armF : armT;
            if (arm < 0)
                continue;
            for (const FnOp& op : blocks[arm].body)
            {
                if (op.code != FnkLocalSet)
                {
                    body.push_back(op);
                    continue;
                }
                std::size_t at = std::find(slots.begin(), slots.end(), op.k) - slots.begin();
                if (at == slots.size())
                {
                    slots.push_back(op.k);
                    valueT.push_back(-1);
                    valueF.push_back(-1);
                }
                (side ? valueF : valueT)[at] = int(op.a);
            }
        }
        if (d.regCnt + 2 * slots.size() > kMaxVReg + 1)
            continue;
        for (std::size_t j = 0; j < slots.size(); ++j)
        {
            if ((valueT[j] < 0) || (valueF[j] < 0))
            {
                (valueT[j] < 0 ? valueT : valueF)[j] = int(d.regCnt);
                body.push_back(MakeOp(FnkLocalGet, 0, 0, 0, d.regCnt++, slots[j]));
            }
            body.push_back(MakeOp(FnkSelect, b.term.a, valueT[j], valueF[j], d.regCnt, 0));
            body.push_back(MakeOp(FnkLocalSet, d.regCnt++, 0, 0, 0, slots[j]));
        }
        b.body = std::move(body);
        b.term = MakeOp(FnkBr, 0, 0, 0, 0, join);
        if (armT >= 0)
            blocks[armT].live = false;
        if (armF >= 0)
            blocks[armF].live = false;
        changed = true;
        survey();
    }
    return changed;
}

/// Short, pure branch arms become selects (see SpeculateRound), innermost first.
bool Speculate(FnDef& d, const vector<FunctionEntry>& functions)
{
    vector<CfgBlock> blocks = Decode(d);
    bool changed = false;
    for (int round = 0; round < 64; ++round)
    {
        if (!SpeculateRound(blocks, d, functions))
            break;
        changed = true;
        Straighten(blocks);
        Forward(blocks, d, functions);
        RemoveDeadOps(blocks, d, functions);
    }
    if (changed)
        Encode(blocks, d);
    return changed;
}

/// Copy propagation, store-to-load forwarding, dead op and dead store removal and jump cleanup.
/// No arithmetic is rewritten or reordered; an op goes only when nothing reads its result.
void Simplify(FnDef& d, const vector<FunctionEntry>& functions)
{
    vector<CfgBlock> blocks = Decode(d);
    RemoveUnreachable(blocks);
    for (int round = 0; round < 16; ++round)
    {
        bool changed = Straighten(blocks);
        changed = Forward(blocks, d, functions) || changed;
        changed = RemoveDeadOps(blocks, d, functions) || changed;
        changed = RemoveDeadStores(blocks, d) || changed;
        if (!changed)
            break;
    }
    if (Retarget(blocks, d, functions))
        RemoveDeadOps(blocks, d, functions);
    Encode(blocks, d);
}

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
    L.def->argBase = functions[fn].fn.return_size;
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
                    const unsigned int code = FnkAddK + (op.code - VM_ADDI);
                    const unsigned int a = L.Reg(op.b);
                    const unsigned int k = L.Imm(op.k);
                    const unsigned int dst = L.Def(op.b);
                    L.Emit(code, a, 0, 0, dst, k);
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
                    const unsigned int k = L.Imm(op.k);
                    const unsigned int d = L.Reg(op.b);
                    const unsigned int gt = L.VReg();
                    const unsigned int eq = L.VReg();
                    L.Emit(FnkCmpGtKA, d, 0, 0, gt, k);
                    L.Emit(FnkCmpEqKA, d, 0, 0, eq, k);
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
                    const unsigned int m = L.VReg();
                    L.Emit(FnkCmpEqKA, a, 0, 0, m, L.ConstIdx(0.0));
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
                    L.Emit(FnkTrap, first, L.self & 0xffffu, L.self >> 16, dst, op.k);
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
                    const unsigned int first = L.Gather(L.sp + retSize, cnt);
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
                    const unsigned int first = L.Gather(window + retSize, cnt);
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

    if (!ValidDef(*L.def, functions))
        return nullptr;
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
        if (!splicable || c.ops.size() > 512 || d.ops.size() + c.ops.size() > 16384 ||
            d.regCnt + c.regCnt + 1 > kMaxVReg + 1)
            continue;

        const unsigned int a0 = d.ops[idx].a;
        const unsigned int window = d.ops[idx].b;
        const unsigned int retSize = d.ops[idx].c;
        const unsigned int dst = d.ops[idx].dst;
        const unsigned int slotBase = d.slotCnt;
        const unsigned int regBase = d.regCnt;
        const unsigned int scratch = regBase + c.regCnt;
        const unsigned int constBase = static_cast<unsigned int>(d.consts.size());
        const unsigned int oldBlocks = static_cast<unsigned int>(d.blockStart.size());
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
            const unsigned int roles = FieldRoles(o.code);
            if (roles & RoleA)      o.a = std::uint16_t(o.a + regBase);
            if (roles & RoleB)      o.b = std::uint16_t(o.b + regBase);
            if (roles & RoleC)      o.c = std::uint16_t(o.c + regBase);
            if (roles & RoleDst)    o.dst = std::uint16_t(o.dst + regBase);
            if (roles & RoleKConst) o.k += constBase;
            if (roles & RoleKSlot)  o.k += slotBase;
            if (roles & RoleKBlock) o.k += entryBlock;
            if (roles & RoleBBlock) o.b = std::uint16_t(o.b + entryBlock);
            return o;
        };

        vector<int> calleeBlockAt(c.ops.size(), -1);
        for (unsigned int m = 0; m < calleeBlocks; ++m)
            calleeBlockAt[c.blockStart[m]] = static_cast<int>(m);

        vector<FnOp> ops;
        vector<unsigned int> bs(oldBlocks + calleeBlocks + 1, 0);
        ops.reserve(d.ops.size() + c.ops.size() + c.parameterCnt + 2 * retSize + 8);
        ops.insert(ops.end(), d.ops.begin(), d.ops.begin() + idx);

        for (unsigned int j = 0; j < c.parameterCnt; ++j)
            ops.push_back(MakeOp(FnkLocalSet, a0 + j, 0, 0, 0, slotBase + c.argBase + j));
        ops.push_back(MakeOp(FnkConst, 0, 0, 0, scratch, scratchConst));
        ops.push_back(MakeOp(FnkLocalSet, scratch, 0, 0, 0, slotBase + c.slotCnt - kSpillHomes));
        ops.push_back(MakeOp(FnkBr, 0, 0, 0, 0, entryBlock));

        for (unsigned int i = 0; i < c.ops.size(); ++i)
        {
            if (calleeBlockAt[i] >= 0)
                bs[oldBlocks + calleeBlockAt[i]] = static_cast<unsigned int>(ops.size());
            if (c.ops[i].code == FnkRet)
            {
                const FnOp ret = remap(c.ops[i]);
                ops.push_back(MakeOp(FnkCopy, ret.a, 0, 0, dst, 0));
                for (unsigned int j = 0; j < retSize; ++j)
                {
                    ops.push_back(MakeOp(FnkLocalGet, 0, 0, 0, scratch, slotBase + j));
                    ops.push_back(MakeOp(FnkLocalSet, scratch, 0, 0, 0, window + j));
                }
                ops.push_back(MakeOp(FnkBr, 0, 0, 0, 0, continuationBlock));
            }
            else
                ops.push_back(remap(c.ops[i]));
        }

        bs[continuationBlock] = static_cast<unsigned int>(ops.size());
        ops.insert(ops.end(), d.ops.begin() + idx + 1, d.ops.end());
        const unsigned int shift = static_cast<unsigned int>(ops.size() - d.ops.size());
        for (unsigned int i = 0; i < oldBlocks; ++i)
            bs[i] = (d.blockStart[i] > idx) ? d.blockStart[i] + shift : d.blockStart[i];

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
    const std::shared_ptr<const FnDef> lowered = LowerFunction(entry.ops, consts, functions, fn);
    entry.ir = lowered;
    if (lowered == nullptr)
        return;
    FnDef d = *lowered;
    for (int round = 0; round < 64; ++round)
        if (!InlineRound(d))
            break;
    if (!ValidDef(d, functions))
        d = *lowered;
    FnDef simplified = d;
    Simplify(simplified, functions);
    if (ValidDef(simplified, functions))
        d = std::move(simplified);
    FnDef speculated = d;
    if (Speculate(speculated, functions) && ValidDef(speculated, functions))
    {
        Simplify(speculated, functions);
        if (ValidDef(speculated, functions))
            d = std::move(speculated);
    }
    entry.ir = std::make_shared<const FnDef>(std::move(d));
}

void FunctionVM::ReserveFrame(FPUContext* ctx, unsigned int need)
{
    if (need > ctx->maxdblstacksize)
    {
        const unsigned int size = max(need, ctx->maxdblstacksize + (unsigned int)INITIAL_DBL_STACK_SIZE);
        ctx->dblstackbase = reinterpret_cast<DBL *>(POV_REALLOC(ctx->dblstackbase, sizeof(DBL) * size, "fn: ir frame"));
        ctx->maxdblstacksize = size;
    }
}

/// Runs a def whose frame is reserved and whose arguments and home are in place.
#if FNIR_THREADED
__attribute__((optimize("no-crossjumping", "no-gcse", "no-tree-slp-vectorize")))
#endif
DBL FunctionVM::ExecIR(FunctionVM* vm, FPUContext* ctx, FUNCTION fnIdx, const FnDef* def, unsigned int frameSp, int depth)
{
    DBL* slots = ctx->dblstackbase + frameSp;
    DBL* regs = slots + def->slotCnt;
    const FnOp* const ops = def->ops.data();
    const DBL* const kConsts = def->consts.data();
    const unsigned int* const kBlocks = def->blockStart.data();
    const FnOp* op = ops;

#define FNIR_OPS(X) \
    X(Const) X(Copy) X(Add) X(Sub) X(Mul) X(Div) X(Mod) X(Neg) X(Abs) \
    X(CmpGt) X(CmpEq) X(MaskOr) X(MaskNot) X(Flag) X(Select) \
    X(AddK) X(SubK) X(MulK) X(DivK) X(ModK) X(CmpGtKA) X(CmpEqKA) \
    X(LocalGet) X(LocalSet) X(Sys1) X(Sys2) X(Math1) X(Math2) X(Trap) X(TrapS) X(Call) \
    X(Br) X(BrCond) X(Ret)

#if FNIR_THREADED
    #define FNIR_LABEL(n) &&L_##n,
    static const void* const dispatchTable[] = { FNIR_OPS(FNIR_LABEL) };
    #undef FNIR_LABEL
    static_assert(sizeof(dispatchTable) / sizeof(dispatchTable[0]) == FnkRet + 1, "one handler per FnOpCode, in order");
    // keeps the table base in a register; otherwise every handler rematerialises it
    const void* const* dispatch = dispatchTable;
    __asm__("" : "+r"(dispatch));
    #define FNIR_HANDLER(n) L_##n:
    #define FNIR_NEXT() { ++op; goto *dispatch[op->code]; }
    #define FNIR_JUMP(block) { op = ops + kBlocks[block]; goto *dispatch[op->code]; }
    goto *dispatch[op->code];
#else
    #define FNIR_HANDLER(n) case Fnk##n:
    #define FNIR_NEXT() { ++op; continue; }
    #define FNIR_JUMP(block) { op = ops + kBlocks[block]; continue; }
    for (;;)
    {
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
    FNIR_HANDLER(AddK)
        regs[op->dst] = regs[op->a] + kConsts[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(SubK)
        regs[op->dst] = regs[op->a] - kConsts[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(MulK)
        regs[op->dst] = regs[op->a] * kConsts[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(DivK)
        regs[op->dst] = regs[op->a] / kConsts[op->k];
        FNIR_NEXT();
    FNIR_HANDLER(ModK)
        regs[op->dst] = fmod(regs[op->a], kConsts[op->k]);
        FNIR_NEXT();
    FNIR_HANDLER(CmpGtKA)
        regs[op->dst] = (kConsts[op->k] > regs[op->a]) ? 1.0 : 0.0;
        FNIR_NEXT();
    FNIR_HANDLER(CmpEqKA)
        regs[op->dst] = (kConsts[op->k] == regs[op->a]) ? 1.0 : 0.0;
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
        const DBL result = POVFPU_TrapTable[op->k].fn(ctx, args, op->b | (unsigned int)(op->c) << 16);
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
        const unsigned int calleeSp = frameSp + def->slotCnt + def->regCnt;
        DBL result = 0.0;
        if (depth + 1 < MAX_CALL_STACK_SIZE)
        {
            ReserveFrame(ctx, calleeSp + callee->slotCnt + callee->regCnt);
            regs = ctx->dblstackbase + frameSp + def->slotCnt;
            DBL* const calleeFrame = ctx->dblstackbase + calleeSp;
            calleeFrame[slotBaseHomes(callee)] = 0.0;
            for (unsigned int j = 0; j < callee->parameterCnt; ++j)
                calleeFrame[callee->argBase + j] = regs[op->a + j];
            result = ExecIR(vm, ctx, op->k, callee, calleeSp, depth + 1);
        }
        else
            POVFPU_Exception(ctx, op->k, "Maximum function evaluation recursion level reached.");
        DBL* const calleeSlots = ctx->dblstackbase + calleeSp;
        slots = ctx->dblstackbase + frameSp;
        for (unsigned int j = 0; j < op->c; ++j)
            slots[op->b + j] = calleeSlots[j];
        regs = slots + def->slotCnt;
        regs[op->dst] = result;
        FNIR_NEXT();
    }
    FNIR_HANDLER(Br)
        FNIR_JUMP(op->k);
    FNIR_HANDLER(BrCond)
        if (regs[op->a] != 0.0)
            FNIR_JUMP(op->k)
        else
            FNIR_JUMP(op->b)
    FNIR_HANDLER(Ret)
        return regs[op->a];

#if !FNIR_THREADED
        }
    }
#endif
#undef FNIR_HANDLER
#undef FNIR_NEXT
#undef FNIR_JUMP
#undef FNIR_OPS
}

DBL POVFPU_RunIR(FPUContext* context, FUNCTION fn)
{
    context->threaddata->Stats()[Ray_Function_VM_Calls]++;
    FunctionVM* const vm = context->functionvm.get();
    const FnDef* def = vm->GetIR(fn);
    if (def == nullptr)
        return POVFPU_RunScalar(context, fn);
    FunctionVM::ReserveFrame(context, def->slotCnt + def->regCnt);
    context->dblstackbase[slotBaseHomes(def)] = 0.0;
    return FunctionVM::ExecIR(vm, context, fn, def, 0, 0);
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
    FunctionVM::ReserveFrame(context, def->slotCnt + def->regCnt);
    for (int i = 0; i < n; ++i)
    {
        // the x, y, z locals as the scalar entry's arguments, then the home a trap-only def returns through
        DBL* const slots = context->dblstackbase;
        slots[0] = x[i];
        slots[1] = y[i];
        slots[2] = z[i];
        slots[slotBaseHomes(def)] = 0.0;
        results[i] = FunctionVM::ExecIR(vm, context, fn, def, 0, 0);
    }
}

}
// end of namespace pov
