#include "vm/fnpovfpu.h"
#include "core/material/noise.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/material/pattern.h"
#include "base/pov_mem.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace pov;

// Exercise real VM instructions and scalar execution; rendered fixtures cover parsing.
struct Program
{
    FunctionVM& vm;
    std::vector<Instruction> code;

    explicit Program(FunctionVM& vm) : vm(vm)
    {
        Emit(OPCODE_GROW, 0, 64);
    }

    void Emit(unsigned int op, unsigned int dst = 0, unsigned int k = 0)
    {
        code.push_back(MAKE_INSTRUCTION(op | dst, k));
    }

    void Register(unsigned int op, unsigned int src, unsigned int dst)
    {
        Emit(op | (src << 3), dst);
    }

    void Constant(unsigned int op, double value, unsigned int dst = 0)
    {
        Emit(op, dst, vm.AddConstant(value));
    }

    void Load(unsigned int slot, unsigned int dst = 0)
    {
        Emit(OPCODE_LOAD | 8, dst, slot);
    }

    void Clamp(double value, bool minimum)
    {
        Constant(OPCODE_LOADI, value, 1);
        Register(OPCODE_CMP, 0, 1);
        Emit(minimum ? OPCODE_BGT : OPCODE_BLT, 0, code.size() + 2);
        Register(OPCODE_MOVE, 1, 0);
    }

    FUNCTION Finish(unsigned char parameters = 3)
    {
        Emit(OPCODE_RTS);
        FunctionCode fn{};
        fn.parameter_cnt = parameters;
        fn.program_size = code.size();
        fn.program = static_cast<Instruction*>(POV_MALLOC(code.size() * sizeof(Instruction), "batch test program"));
        std::memcpy(fn.program, code.data(), code.size() * sizeof(Instruction));
        return vm.AddFunction(&fn);
    }
};

struct Example
{
    std::string name;
    FUNCTION fn;
    bool supported;
    bool evaluate;
    bool preferred = false;
};

static std::vector<Example> Examples(FunctionVM& vm)
{
    std::vector<Example> cases;
    Program affine(vm);
    affine.Load(0);
    affine.Constant(OPCODE_MULI, 2.5);
    affine.Load(1, 1);
    affine.Register(OPCODE_ADD, 1, 0);
    affine.Constant(OPCODE_SUBI, 0.125);
    cases.push_back({"affine", affine.Finish(), true, true});

    Program clamp(vm);
    clamp.Load(1);
    clamp.Clamp(1.0, true);
    clamp.Clamp(0.0, false);
    cases.push_back({"divergent clamp", clamp.Finish(), true, true});

    Program product(vm);
    product.Load(0);
    product.Load(1, 1);
    product.Register(OPCODE_MUL, 1, 0);
    product.Constant(OPCODE_DIVI, -3.0);
    cases.push_back({"product", product.Finish(), true, true});

    for(unsigned int op : {TRAP_SYS1_SIN, TRAP_SYS1_COS, TRAP_SYS1_SQRT,
                           TRAP_SYS1_EXP, TRAP_SYS1_LN, TRAP_SYS1_FLOOR})
    {
        Program math(vm);
        math.Load(0);
        math.Register(OPCODE_ABS, 0, 0);
        math.Constant(OPCODE_ADDI, 0.125);
        math.Emit(OPCODE_SYS1, 0, op);
        cases.push_back({"math " + std::to_string(op), math.Finish(), true, true, op != TRAP_SYS1_SQRT && op != TRAP_SYS1_FLOOR});
    }
    for(unsigned int op : {TRAP_SYS2_POW, TRAP_SYS2_ATAN2, TRAP_SYS2_MOD, TRAP_SYS2_DIV})
    {
        Program math(vm);
        math.Load(0);
        math.Register(OPCODE_ABS, 0, 0);
        math.Constant(OPCODE_ADDI, 0.125);
        math.Constant(OPCODE_LOADI, 1.25, 1);
        math.Emit(OPCODE_SYS2, 0, op);
        cases.push_back({"math2 " + std::to_string(op), math.Finish(), true, true, op == TRAP_SYS2_POW || op == TRAP_SYS2_ATAN2});
    }

    Program noise(vm);
    noise.code.clear();
    noise.Emit(OPCODE_TRAP, 0, 76);
    const FUNCTION noiseFn = noise.Finish();
    cases.push_back({"noise", noiseFn, true, true});
    Program wrapper(vm);
    wrapper.Emit(OPCODE_STORE | 8, 5, 3);
    for(unsigned int axis = 0; axis < 3; ++axis)
    {
        wrapper.Load(axis);
        wrapper.Emit(OPCODE_STORE | 8, 0, 4 + axis);
    }
    wrapper.Emit(OPCODE_PUSH, 0, 4);
    wrapper.Emit(OPCODE_CALL, 0, noiseFn);
    wrapper.Emit(OPCODE_POP, 0, 4);
    wrapper.Load(3, 5);
    for(unsigned int axis = 0; axis < 3; ++axis)
        wrapper.Load(axis, 2 + axis);
    cases.push_back({"saved-register noise wrapper", wrapper.Finish(), true, true});

    FUNCTION multipleNoise = noiseFn;
    for(unsigned int calls = 1; calls <= 2; ++calls)
    {
        Program mixed(vm);
        for(unsigned int call = 0; call < calls; ++call)
        {
            for(unsigned int axis = 0; axis < 3; ++axis)
            {
                mixed.Load(axis);
                mixed.Emit(OPCODE_STORE | 8, 0, 4 + axis);
            }
            mixed.Emit(OPCODE_PUSH, 0, 4);
            mixed.Emit(OPCODE_CALL, 0, noiseFn);
            mixed.Emit(OPCODE_POP, 0, 4);
        }
        mixed.Emit(OPCODE_SYS1, 0, TRAP_SYS1_SIN);
        multipleNoise = mixed.Finish();
        cases.push_back({"math with noise calls " + std::to_string(calls), multipleNoise, true, true, calls == 1});
    }
    Program nestedNoise(vm);
    nestedNoise.Emit(OPCODE_PUSH, 0, 0);
    nestedNoise.Emit(OPCODE_CALL, 0, multipleNoise);
    nestedNoise.Emit(OPCODE_POP, 0, 0);
    nestedNoise.Emit(OPCODE_SYS1, 0, TRAP_SYS1_COS);
    cases.push_back({"nested multi-noise maths", nestedNoise.Finish(), true, true, false});

    Program generator(vm);
    generator.code.clear();
    generator.Emit(OPCODE_TRAP, 0, 78);
    const FUNCTION generatorFn = generator.Finish(4);
    cases.push_back({"four-argument root", generatorFn, false, false});
    for(int gen = 1; gen <= 3; ++gen)
    {
        Program explicitNoise(vm);
        explicitNoise.Constant(OPCODE_LOADI, gen);
        explicitNoise.Emit(OPCODE_STORE | 8, 0, 3);
        explicitNoise.Emit(OPCODE_PUSH, 0, 0);
        explicitNoise.Emit(OPCODE_CALL, 0, generatorFn);
        explicitNoise.Emit(OPCODE_POP, 0, 0);
        cases.push_back({"explicit noise generator " + std::to_string(gen), explicitNoise.Finish(), true, true});
    }

    for(double value : {-2.0, -0.0, 0.0, std::nextafter(1.0, 0.0), 1.0,
                        std::nextafter(1.0, 2.0), 2.0, 2.5})
    {
        Program constant(vm);
        constant.Constant(OPCODE_LOADI, value);
        cases.push_back({"wrap " + std::to_string(value), constant.Finish(), true, true});
    }

    Program global(vm);
    global.Emit(OPCODE_LOAD, 0, 0);
    global.Constant(OPCODE_ADDI, 0.125);
    global.Emit(OPCODE_STORE, 0, 0);
    cases.push_back({"mutable global fallback", global.Finish(), false, true});
    for(unsigned int trap : {59u, 77u})
    {
        Program unknown(vm);
        unknown.code.clear();
        unknown.Emit(OPCODE_TRAP, 0, trap);
        cases.push_back({"stateful trap " + std::to_string(trap), unknown.Finish(16), false, false});
    }
    Program undefined(vm);
    undefined.Load(9);
    cases.push_back({"uninitialized local", undefined.Finish(), false, false});
    Program branch(vm);
    branch.Load(0);
    branch.Constant(OPCODE_CMPI, 0.0);
    branch.Emit(OPCODE_BEQ, 0, branch.code.size() + 2);
    branch.Emit(OPCODE_STORE | 8, 0, 9);
    branch.Load(9);
    cases.push_back({"partially initialized local", branch.Finish(), false, false});
    Program large(vm);
    large.code[0] = MAKE_INSTRUCTION(OPCODE_GROW, 1024);
    large.Load(0);
    cases.push_back({"large local frame", large.Finish(), false, false});
    Program unpaired(vm);
    unpaired.Emit(OPCODE_PUSH, 0, 4);
    unpaired.Load(0);
    cases.push_back({"unpaired stack change", unpaired.Finish(), false, false});

    Program growingChild(vm);
    growingChild.code[0] = MAKE_INSTRUCTION(OPCODE_GROW, 255);
    growingChild.Load(0);
    growingChild.Constant(OPCODE_ADDI, 0.125);
    growingChild.Emit(OPCODE_SYS1, 0, TRAP_SYS1_SIN);
    const FUNCTION growingFn = growingChild.Finish();
    Program growingCall(vm);
    growingCall.code[0] = MAKE_INSTRUCTION(OPCODE_GROW, 255);
    for(unsigned int axis = 0; axis < 3; ++axis)
    {
        growingCall.Load(axis);
        growingCall.Emit(OPCODE_STORE | 8, 0, 250 + axis);
    }
    growingCall.Emit(OPCODE_PUSH, 0, 250);
    growingCall.Emit(OPCODE_CALL, 0, growingFn);
    growingCall.Emit(OPCODE_POP, 0, 250);
    cases.push_back({"callee stack growth", growingCall.Finish(), true, true, true});

    FUNCTION previous = growingFn;
    for(unsigned int depth = 2; depth <= 65; ++depth)
    {
        Program call(vm);
        call.Emit(OPCODE_PUSH, 0, 0);
        call.Emit(OPCODE_CALL, 0, previous);
        call.Emit(OPCODE_POP, 0, 0);
        previous = call.Finish();
        if(depth >= 64)
            cases.push_back({"call depth " + std::to_string(depth), previous, depth == 64, true, depth == 64});
    }
    return cases;
}

static bool Same(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || std::memcmp(&a, &b, sizeof(a)) == 0;
}

static bool CheckLifecycle(FunctionVM& vm, TraceThreadData& thread)
{
    Program initial(vm);
    initial.Load(0);
    initial.Emit(OPCODE_SYS1, 0, TRAP_SYS1_SIN);
    FUNCTION slot = initial.Finish();
    {
        FunctionPattern pattern;
        pattern.pFn = new FunctionVM::CustomFunction(&vm, vm.CopyFunction(&slot));
        std::unique_ptr<GenericScalarFunction> clone(pattern.pFn->Clone());
        vm.RemoveFunction(slot);
        GenericScalarFunctionInstance instance(clone.get(), &thread);
        if(!clone->CanExecuteBatch() || !clone->PreferBatch() || !pattern.pFn->CanExecuteBatch() ||
           !pattern.pFn->PreferBatch() || instance.Evaluate(0.0, 0.5, 0.75) != 0.0)
            return false;
    }
    Program replacement(vm);
    replacement.Emit(OPCODE_LOAD, 0, 0);
    const FUNCTION reused = replacement.Finish();
    if(reused != slot || vm.CanExecuteBatch(reused))
        return false;
    vm.RemoveFunction(reused);
    Program restored(vm);
    restored.Load(1);
    const FUNCTION again = restored.Finish();
    if(again != slot || !vm.CanExecuteBatch(again) || vm.PreferBatch(again))
        return false;
    vm.RemoveFunction(again);
    return true;
}

int main()
{
    Initialize_Noise();
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    const auto cases = Examples(*vm);
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1);
    if(!CheckLifecycle(*vm, thread))
    {
        std::cerr << "function clone/removal/reuse failed\n";
        return 1;
    }
    std::mt19937_64 rng(0x62617463686573ULL);
    std::uniform_real_distribution<double> coordinate(-8.0, 8.0);
    std::vector<Vector3d> points;
    for(double edge : {-2.0, -0.0, 0.0, std::nextafter(1.0, 0.0), 1.0,
                       std::nextafter(1.0, 2.0), 2.0})
        points.emplace_back(edge, edge, edge);
    for(int i = 0; i < 64; ++i)
        points.emplace_back(coordinate(rng), coordinate(rng), coordinate(rng));
    FPUContext directContext(vm.get(), &thread);
    size_t comparisons = 0, simdComparisons = 0;
    for(const auto& example : cases)
    {
        FunctionPattern pattern;
        FUNCTION fn = example.fn;
        pattern.pFn = new FunctionVM::CustomFunction(vm.get(), vm->CopyFunction(&fn));
        if(pattern.pFn->CanExecuteBatch() != example.supported || pattern.pFn->PreferBatch() != example.preferred)
        {
            std::cerr << example.name << ": unexpected batch eligibility\n";
            return 1;
        }
        if(!example.evaluate)
            continue;
        for(int generator = 1; generator <= 3; ++generator)
        {
            scene->noiseGenerator = generator;
            for(unsigned char wave : {kWaveType_Raw, kWaveType_Ramp, kWaveType_Sine, kWaveType_Triangle})
            {
                pattern.waveType = wave;
                pattern.waveFrequency = 1.25;
                pattern.wavePhase = 0.125;
                for(size_t count : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 16u, 17u, 71u})
                {
                    for(size_t first = 0; first + count <= points.size(); first += count ? count : points.size() + 1)
                    {
                        std::vector<double> reference(count), actual(count + 1, -12345.0);
                        vm->SetGlobal(0, 0.0);
                        for(size_t i = 0; i < count; ++i)
                            reference[i] = pattern.Evaluate(points[first + i], nullptr, nullptr, &thread);
                        const double referenceGlobal = vm->GetGlobal(0);
                        vm->SetGlobal(0, 0.0);
                        pattern.EvaluateBatch(points.data() + first, actual.data(), count, &thread);
                        if(!Same(referenceGlobal, vm->GetGlobal(0)) || actual[count] != -12345.0)
                        {
                            std::cerr << example.name << ": state or output boundary changed\n";
                            return 1;
                        }
                        if(example.supported && count)
                        {
                            std::vector<double> x(count), y(count), z(count), direct(count);
                            for(size_t i = 0; i < count; ++i)
                            {
                                x[i] = points[first + i].x();
                                y[i] = points[first + i].y();
                                z[i] = points[first + i].z();
                            }
                            POVFPU_RunBatch(&directContext, fn, x.data(), y.data(), z.data(), direct.data(), int(count));
                            for(size_t i = 0; i < count; ++i)
                            {
                                ++simdComparisons;
                                const double wrapped = direct[i] > 1.0 ? std::fmod(direct[i], 1.0) : direct[i];
                                if(!Same(reference[i], pattern.Wave(wrapped)))
                                {
                                    std::cerr << example.name << ": direct SIMD mismatch\n";
                                    return 1;
                                }
                            }
                        }
                        for(size_t i = 0; i < count; ++i)
                        {
                            ++comparisons;
                            if(!Same(reference[i], actual[i]))
                            {
                                std::cerr << example.name << ": mismatch, generator " << generator
                                          << ", wave " << unsigned(wave) << ", batch " << count
                                          << ", point " << first + i << ": " << std::hexfloat
                                          << reference[i] << " != " << actual[i] << "\n";
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    std::cout << cases.size() << " eligibility cases; " << comparisons << " pattern comparisons; "
              << simdComparisons << " direct SIMD comparisons passed\n";
}
