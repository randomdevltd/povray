//******************************************************************************
///
/// @file tests/source/tests_fnbatch.cpp
///
/// POV-Ray unit tests for the batched function interpreter (@ref vm/fnpovfpu.h).
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

// configbase.h must always be the first POV file included; tests.h must follow suite.
#include "base/configbase.h"
#include "tests.h"

#include "base/pov_mem.h"
#include "core/material/noise.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "vm/fnpovfpu.h"

// this must be the last file included
#include "base/povdebug.h"

using namespace pov;

namespace
{

struct Asm
{
    std::vector<Instruction> code;
    void op(unsigned int o, unsigned int rs = 0, unsigned int rd = 0, unsigned int k = 0) { code.push_back(MAKE_INSTRUCTION(o | (rs << 3) | rd, k)); }
    void loadXYZ() { op(OPCODE_LOAD, 1, 2, 0); op(OPCODE_LOAD, 1, 3, 1); op(OPCODE_LOAD, 1, 4, 2); }
};

FUNCTION Add(FunctionVM& vm, const Asm& a, unsigned char parameters = 3, unsigned char returns = 0)
{
    FunctionCode f = FunctionCode();
    f.program_size = (unsigned int)a.code.size();
    f.program = reinterpret_cast<Instruction *>(POV_MALLOC(sizeof(Instruction) * a.code.size(), "test program"));
    std::memcpy(f.program, a.code.data(), sizeof(Instruction) * a.code.size());
    f.parameter_cnt = parameters;
    f.return_size = returns;
    return vm.AddFunction(&f);
}

/// Ten awkward values and a fixed pseudo-random spread, so every lane pattern of branches comes up.
std::vector<DBL> Values(int n, unsigned int seed)
{
    const DBL special[] = { 0.0, -0.0, 1.0, -2.5, 3.75, 1e300, -1e-310, std::numeric_limits<DBL>::infinity(),
                            -std::numeric_limits<DBL>::infinity(), std::numeric_limits<DBL>::quiet_NaN() };
    std::vector<DBL> v(n);
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        v[i] = (i % 3 == 0) ? special[(i / 3) % 10] : (DBL(seed >> 8) / DBL(1u << 24)) * 8.0 - 4.0;
    }
    return v;
}

bool Same(DBL a, DBL b)
{
    return (std::isnan(a) && std::isnan(b)) || (std::memcmp(&a, &b, sizeof(DBL)) == 0);
}

void CheckBatchMatchesScalar(FunctionVM& vm, FUNCTION fn)
{
    static const bool noiseReady = (Initialize_Noise(), true);
    (void)noiseReady;
    TraceThreadData thread(std::make_shared<SceneData>(), 1);
    FPUContext ctx(&vm, &thread);
    const int kPoints = 67;
    const std::vector<DBL> x = Values(kPoints, 1), y = Values(kPoints, 2), z = Values(kPoints, 3);
    std::vector<DBL> scalar(kPoints), batch(kPoints);
    for (int i = 0; i < kPoints; ++i)
    {
        ctx.SetLocal(0, x[i]);
        ctx.SetLocal(1, y[i]);
        ctx.SetLocal(2, z[i]);
        scalar[i] = POVFPU_RunDefault(&ctx, fn);
    }
    const int sizes[] = { 1, 2, 3, 4, 5, 7, 8, 9, kPoints };
    for (int n : sizes)
    {
        for (int first = 0; first + n <= kPoints; first += n)
        {
            POVFPU_RunBatch(&ctx, fn, &x[first], &y[first], &z[first], &batch[first], n);
            for (int i = first; i < first + n; ++i)
                BOOST_CHECK_MESSAGE(Same(batch[i], scalar[i]), "point " << i << " of a batch of " << n << ": " << batch[i] << " != " << scalar[i]);
        }
    }
}

/// Runs `fn` one point at a time and in batches of four over `n` points spread across [lo, hi] in x and [ylo, yhi] in y,
/// and checks both against `ref` within 1e-12 relative to max(1, |ref|).
template<typename F>
void CheckMathsAgainstStd(FunctionVM& vm, FUNCTION fn, F ref, DBL lo, DBL hi, DBL ylo, DBL yhi, const char *name)
{
    static const bool noiseReady = (Initialize_Noise(), true);
    (void)noiseReady;
    TraceThreadData thread(std::make_shared<SceneData>(), 1);
    FPUContext ctx(&vm, &thread);
    const int kPoints = 4000;
    std::vector<DBL> x(kPoints), y(kPoints), z(kPoints, 0.0), batch(kPoints);
    for (int i = 0; i < kPoints; ++i)
    {
        x[i] = lo + (hi - lo) * (i + 0.37) / kPoints;
        y[i] = ylo + (yhi - ylo) * ((i * 7919) % kPoints + 0.61) / kPoints;
    }
    for (int first = 0; first < kPoints; first += 4)
        POVFPU_RunBatch(&ctx, fn, &x[first], &y[first], &z[first], &batch[first], 4);
    for (int i = 0; i < kPoints; ++i)
    {
        ctx.SetLocal(0, x[i]);
        ctx.SetLocal(1, y[i]);
        ctx.SetLocal(2, z[i]);
        const DBL one = POVFPU_RunDefault(&ctx, fn), want = ref(x[i], y[i]);
        const DBL tol = 1e-12 * std::max(DBL(1.0), std::fabs(want));
        BOOST_CHECK_MESSAGE(std::fabs(one - want) <= tol, name << "(" << x[i] << ", " << y[i] << ") = " << one << ", std " << want);
        BOOST_CHECK_MESSAGE(Same(batch[i], one), name << " batch " << batch[i] << " != one point " << one);
    }
}

}
// end of anonymous namespace

BOOST_AUTO_TEST_SUITE( FunctionBatch )

BOOST_AUTO_TEST_CASE( ArithmeticFlagsAndLibraryCalls )
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    Asm a;
    a.op(OPCODE_GROW, 0, 0, 8);
    a.loadXYZ();
    a.op(OPCODE_MOVE, 2, 0); a.op(OPCODE_MUL, 3, 0); a.op(OPCODE_SYS1, 0, 0, TRAP_SYS1_SIN); a.op(OPCODE_MOVE, 0, 5);
    a.op(OPCODE_MOVE, 4, 0); a.op(OPCODE_LOADI, 0, 1, vm->AddConstant(2.0)); a.op(OPCODE_SYS2, 0, 0, TRAP_SYS2_POW); a.op(OPCODE_ADD, 0, 5);
    a.op(OPCODE_MOVE, 2, 0); a.op(OPCODE_ABS, 0, 0); a.op(OPCODE_SYS1, 0, 0, TRAP_SYS1_SQRT); a.op(OPCODE_ADD, 0, 5);
    a.op(OPCODE_MOVE, 3, 6); a.op(OPCODE_DIV, 4, 6); a.op(OPCODE_CMP, 6, 5); a.op(OPCODE_SGE, 0, 7); a.op(OPCODE_ADD, 7, 5);
    a.op(OPCODE_MOVE, 2, 0); a.op(OPCODE_LOADI, 0, 1, vm->AddConstant(1.5)); a.op(OPCODE_MOD, 1, 0); a.op(OPCODE_ADD, 0, 5);
    a.op(OPCODE_TEQ, 0, 6); a.op(OPCODE_ADD, 6, 5); a.op(OPCODE_XEQ, 0, 1);
    a.op(OPCODE_NEG, 5, 0); a.op(OPCODE_RTS);
    CheckBatchMatchesScalar(*vm, Add(*vm, a));
}

BOOST_AUTO_TEST_CASE( LanesLoopingAndBranchingApart )
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    Asm a;
    a.op(OPCODE_GROW, 0, 0, 8);
    a.loadXYZ();
    a.op(OPCODE_MOVE, 2, 7); a.op(OPCODE_ABS, 7, 7); a.op(OPCODE_LOADI, 0, 1, vm->AddConstant(4.0)); a.op(OPCODE_MOD, 1, 7);
    a.op(OPCODE_NEG, 7, 7); a.op(OPCODE_LOADI, 0, 6, vm->AddConstant(0.0));
    a.op(OPCODE_CMPI, 0, 7, vm->AddConstant(0.0));                 // 10: loop while r7 < 0
    a.op(OPCODE_BLT, 0, 0, 13); a.op(OPCODE_JMP, 0, 0, 16);
    a.op(OPCODE_ADD, 2, 6); a.op(OPCODE_ADDI, 0, 7, vm->AddConstant(1.0)); a.op(OPCODE_JMP, 0, 0, 10);
    a.op(OPCODE_CMPI, 0, 3, vm->AddConstant(0.0));                 // 16: select(y, z, y)
    a.op(OPCODE_BGT, 0, 0, 20); a.op(OPCODE_MOVE, 4, 0); a.op(OPCODE_JMP, 0, 0, 21); a.op(OPCODE_MOVE, 3, 0);
    a.op(OPCODE_ADD, 6, 0); a.op(OPCODE_RTS);
    CheckBatchMatchesScalar(*vm, Add(*vm, a));
}

BOOST_AUTO_TEST_CASE( CallsAndTrapsFromLanesApart )
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    Asm b;
    b.op(OPCODE_GROW, 0, 0, 4);
    b.loadXYZ();
    b.op(OPCODE_MOVE, 3, 0); b.op(OPCODE_MOVE, 4, 1); b.op(OPCODE_SYS2, 0, 0, TRAP_SYS2_ATAN2); b.op(OPCODE_ADD, 2, 0); b.op(OPCODE_RTS);
    const FUNCTION callee = Add(*vm, b);
    Asm s;
    s.op(OPCODE_TRAP, 0, 0, 61); s.op(OPCODE_RTS);                // f_sphere(x, y, z, radius)
    const FUNCTION sphere = Add(*vm, s, 4);
    Asm t;
    t.op(OPCODE_TRAPS, 0, 0, 1); t.op(OPCODE_RTS);                // f_transform without a transform: zeros
    const FUNCTION transform = Add(*vm, t, 3, 3);

    Asm a;
    a.op(OPCODE_GROW, 0, 0, 20);
    a.loadXYZ();
    a.op(OPCODE_CMPI, 0, 2, vm->AddConstant(0.0)); a.op(OPCODE_BGT, 0, 0, 14);
    a.op(OPCODE_STORE, 1, 4, 8); a.op(OPCODE_STORE, 1, 2, 9); a.op(OPCODE_STORE, 1, 3, 10);
    a.op(OPCODE_PUSH, 0, 0, 8); a.op(OPCODE_CALL, 0, 0, callee); a.op(OPCODE_POP, 0, 0, 8);
    a.op(OPCODE_ADDI, 0, 0, vm->AddConstant(1.0)); a.op(OPCODE_JMP, 0, 0, 21);
    a.op(OPCODE_STORE, 1, 2, 8); a.op(OPCODE_STORE, 1, 3, 9); a.op(OPCODE_STORE, 1, 4, 10);   // 14
    a.op(OPCODE_PUSH, 0, 0, 8); a.op(OPCODE_CALL, 0, 0, callee); a.op(OPCODE_POP, 0, 0, 8);
    a.op(OPCODE_MULI, 0, 0, vm->AddConstant(2.0));
    a.op(OPCODE_MOVE, 0, 5); a.op(OPCODE_STORE, 1, 5, 3);                                    // 21
    a.loadXYZ();
    a.op(OPCODE_STORE, 1, 2, 8); a.op(OPCODE_STORE, 1, 3, 9); a.op(OPCODE_STORE, 1, 4, 10);
    a.op(OPCODE_LOADI, 0, 0, vm->AddConstant(1.5)); a.op(OPCODE_STORE, 1, 0, 11);
    a.op(OPCODE_PUSH, 0, 0, 8); a.op(OPCODE_CALL, 0, 0, sphere); a.op(OPCODE_POP, 0, 0, 8);
    a.op(OPCODE_LOAD, 1, 5, 3); a.op(OPCODE_ADD, 0, 5);
    a.op(OPCODE_STORE, 1, 2, 11); a.op(OPCODE_STORE, 1, 3, 12); a.op(OPCODE_STORE, 1, 4, 13);
    a.op(OPCODE_PUSH, 0, 0, 8); a.op(OPCODE_CALL, 0, 0, transform); a.op(OPCODE_POP, 0, 0, 8);
    a.op(OPCODE_LOAD, 1, 0, 8); a.op(OPCODE_ADD, 0, 5); a.op(OPCODE_LOAD, 1, 0, 10); a.op(OPCODE_ADD, 0, 5);
    a.op(OPCODE_MOVE, 5, 0); a.op(OPCODE_RTS);
    CheckBatchMatchesScalar(*vm, Add(*vm, a));
}

BOOST_AUTO_TEST_CASE( GlobalStoresFallBackToScalar )
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    vm->SetGlobal(0, 0.0);
    Asm a;
    a.op(OPCODE_GROW, 0, 0, 4);
    a.loadXYZ();
    a.op(OPCODE_STORE, 0, 2, 0); a.op(OPCODE_LOAD, 0, 0, 0); a.op(OPCODE_MUL, 3, 0); a.op(OPCODE_RTS);
    CheckBatchMatchesScalar(*vm, Add(*vm, a));
}

BOOST_AUTO_TEST_CASE( LibraryMathsMatchStd )
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    auto one = [&](unsigned int k) { Asm a; a.op(OPCODE_GROW, 0, 0, 8); a.loadXYZ(); a.op(OPCODE_MOVE, 2, 0);
                                     a.op(OPCODE_SYS1, 0, 0, k); a.op(OPCODE_RTS); return Add(*vm, a); };
    auto two = [&](unsigned int k) { Asm a; a.op(OPCODE_GROW, 0, 0, 8); a.loadXYZ(); a.op(OPCODE_MOVE, 2, 0); a.op(OPCODE_MOVE, 3, 1);
                                     a.op(OPCODE_SYS2, 0, 0, k); a.op(OPCODE_RTS); return Add(*vm, a); };
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_SIN), [](DBL x, DBL) { return std::sin(x); }, -200, 200, 0, 0, "sin");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_COS), [](DBL x, DBL) { return std::cos(x); }, -200, 200, 0, 0, "cos");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_TAN), [](DBL x, DBL) { return std::tan(x); }, -200, 200, 0, 0, "tan");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ASIN), [](DBL x, DBL) { return std::asin(x); }, -1, 1, 0, 0, "asin");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ACOS), [](DBL x, DBL) { return std::acos(x); }, -1, 1, 0, 0, "acos");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ATAN), [](DBL x, DBL) { return std::atan(x); }, -200, 200, 0, 0, "atan");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_SINH), [](DBL x, DBL) { return std::sinh(x); }, -200, 200, 0, 0, "sinh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_COSH), [](DBL x, DBL) { return std::cosh(x); }, -200, 200, 0, 0, "cosh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_TANH), [](DBL x, DBL) { return std::tanh(x); }, -200, 200, 0, 0, "tanh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ASINH), [](DBL x, DBL) { return std::asinh(x); }, -200, 200, 0, 0, "asinh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ACOSH), [](DBL x, DBL) { return std::acosh(x); }, 1, 200, 0, 0, "acosh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_ATANH), [](DBL x, DBL) { return std::atanh(x); }, -0.999, 0.999, 0, 0, "atanh");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_EXP), [](DBL x, DBL) { return std::exp(x); }, -200, 200, 0, 0, "exp");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_LN), [](DBL x, DBL) { return std::log(x); }, 1e-6, 200, 0, 0, "log");
    CheckMathsAgainstStd(*vm, one(TRAP_SYS1_LOG), [](DBL x, DBL) { return std::log10(x); }, 1e-6, 200, 0, 0, "log10");
    CheckMathsAgainstStd(*vm, two(TRAP_SYS2_POW), [](DBL x, DBL y) { return std::pow(x, y); }, 1e-3, 10, -4, 4, "pow");
    CheckMathsAgainstStd(*vm, two(TRAP_SYS2_ATAN2), [](DBL x, DBL y) { return std::atan2(x, y); }, -200, 200, -200, 200, "atan2");
}

BOOST_AUTO_TEST_SUITE_END()
