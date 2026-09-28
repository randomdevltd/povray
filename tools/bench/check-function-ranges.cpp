#include "vm/fnpovfpu.h"
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

    FUNCTION Finish()
    {
        Emit(OPCODE_RTS);
        FunctionCode fn{};
        fn.parameter_cnt = 3;
        fn.program_size = code.size();
        fn.program = static_cast<Instruction*>(POV_MALLOC(code.size() * sizeof(Instruction), "range test program"));
        std::memcpy(fn.program, code.data(), code.size() * sizeof(Instruction));
        return vm.AddFunction(&fn);
    }
};

struct Example
{
    std::string name;
    FUNCTION fn;
    bool supported;
};

static std::vector<Example> Examples(FunctionVM& vm)
{
    std::vector<Example> cases;
    Program affine(vm);
    affine.Load(0);
    affine.Constant(OPCODE_MULI, 2.5);
    affine.Constant(OPCODE_ADDI, -0.75);
    affine.Load(1, 1);
    affine.Constant(OPCODE_MULI, -0.3, 1);
    affine.Register(OPCODE_ADD, 1, 0);
    affine.Load(2, 1);
    affine.Constant(OPCODE_MULI, 0.1, 1);
    affine.Register(OPCODE_ADD, 1, 0);
    cases.push_back({"affine", affine.Finish(), true});

    Program clamp(vm);
    clamp.Load(1);
    clamp.Constant(OPCODE_SUBI, 0.25);
    clamp.Constant(OPCODE_MULI, 0.5);
    clamp.Clamp(1.0, true);
    clamp.Clamp(0.0, false);
    const FUNCTION clamped = clamp.Finish();
    cases.push_back({"height clamp", clamped, true});

    Program tent(vm);
    tent.Load(1);
    tent.Register(OPCODE_ABS, 0, 0);
    tent.Register(OPCODE_NEG, 0, 0);
    tent.Constant(OPCODE_ADDI, 1.0);
    tent.Clamp(0.0, false);
    cases.push_back({"absolute tent", tent.Finish(), true});

    Program product(vm);
    product.Load(0);
    product.Load(1, 1);
    product.Register(OPCODE_MUL, 1, 0);
    product.Constant(OPCODE_DIVI, -3.0);
    cases.push_back({"signed product", product.Finish(), true});

    Program minimum(vm);
    minimum.Load(0);
    minimum.Load(1, 1);
    minimum.Register(OPCODE_CMP, 0, 1);
    minimum.Emit(OPCODE_BGT, 0, minimum.code.size() + 2);
    minimum.Register(OPCODE_MOVE, 1, 0);
    cases.push_back({"two-variable minimum", minimum.Finish(), true});

    Program stored(vm);
    stored.Load(2);
    stored.Emit(OPCODE_STORE | 8, 0, 10);
    stored.Constant(OPCODE_LOADI, 3.0);
    stored.Load(10, 1);
    stored.Register(OPCODE_SUB, 1, 0);
    cases.push_back({"local storage", stored.Finish(), true});

    Program call(vm);
    call.Emit(OPCODE_PUSH, 0, 0);
    call.Emit(OPCODE_CALL, 0, clamped);
    call.Emit(OPCODE_POP, 0, 0);
    cases.push_back({"clamp wrapper", call.Finish(), true});

    Program savedCall(vm);
    savedCall.Emit(OPCODE_STORE | 8, 5, 3);
    for(unsigned int axis = 0; axis < 3; ++axis)
    {
        savedCall.Load(axis);
        savedCall.Emit(OPCODE_STORE | 8, 0, 4 + axis);
    }
    savedCall.Emit(OPCODE_PUSH, 0, 4);
    savedCall.Emit(OPCODE_CALL, 0, clamped);
    savedCall.Emit(OPCODE_POP, 0, 4);
    savedCall.Load(3, 5);
    for(unsigned int axis = 0; axis < 3; ++axis)
        savedCall.Load(axis, 2 + axis);
    cases.push_back({"saved-register clamp wrapper", savedCall.Finish(), true});

    for(double value : {-2.0, 0.0, std::nextafter(1.0, 0.0), 1.0,
                        std::nextafter(1.0, 2.0), 2.0, 2.5})
    {
        Program constant(vm);
        constant.Constant(OPCODE_LOADI, value);
        cases.push_back({"constant " + std::to_string(value), constant.Finish(), true});
    }

    Program sine(vm);
    sine.Load(0);
    sine.Emit(OPCODE_SYS1, 0, TRAP_SYS1_SIN);
    cases.push_back({"sine fallback", sine.Finish(), false});

    Program division(vm);
    division.Load(0);
    division.Load(1, 1);
    division.Emit(OPCODE_XEQ, 1);
    division.Register(OPCODE_DIV, 1, 0);
    cases.push_back({"variable division fallback", division.Finish(), false});

    Program noise(vm);
    noise.Emit(OPCODE_TRAP, 0, 76);
    cases.push_back({"noise fallback", noise.Finish(), false});

    Program tooLarge(vm);
    tooLarge.Load(0);
    for(int i = 0; i < 150; ++i)
        tooLarge.Constant(OPCODE_ADDI, 0.125);
    cases.push_back({"node limit fallback", tooLarge.Finish(), false});

    Program nonfinite(vm);
    nonfinite.Constant(OPCODE_LOADI, std::numeric_limits<double>::infinity());
    cases.push_back({"nonfinite constant fallback", nonfinite.Finish(), false});
    return cases;
}

static bool Contains(double lo, double hi, double value)
{
    return std::isfinite(lo) && std::isfinite(hi) && std::isfinite(value) && lo <= value && value <= hi;
}

int main()
{
    boost::intrusive_ptr<FunctionVM> vm(new FunctionVM());
    FPUContext context(vm.get(), nullptr);
    const auto cases = Examples(*vm);
    std::mt19937_64 rng(0x72616e676573ULL);
    std::uniform_real_distribution<double> coordinate(-8.0, 8.0);
    std::vector<std::pair<Vector3d, Vector3d>> segments;
    segments.emplace_back(Vector3d(0.0, 1.0, 0.0), Vector3d(1.0, 0.0, 0.0));
    for(int i = 0; i < 256; ++i)
        segments.emplace_back(Vector3d(coordinate(rng), coordinate(rng), coordinate(rng)),
                              Vector3d(coordinate(rng), coordinate(rng), coordinate(rng)));
    for(double edge : {-2.0, 0.0, 0.25, 1.0, 2.0, 2.25})
    {
        const double before = std::nextafter(edge, -std::numeric_limits<double>::infinity());
        const double after = std::nextafter(edge, std::numeric_limits<double>::infinity());
        segments.emplace_back(Vector3d(before, before, before), Vector3d(after, after, after));
        segments.emplace_back(Vector3d(edge, edge, edge), Vector3d(edge, edge, edge));
    }

    size_t queries = 0, samples = 0;
    for(const auto& example : cases)
    {
        FunctionPattern pattern;
        FUNCTION fn = example.fn;
        pattern.pFn = new FunctionVM::CustomFunction(vm.get(), vm->CopyFunction(&fn));
        for(const auto& segment : segments)
        {
            double lo, hi, rawLo, rawHi;
            const bool supported = vm->EvaluateRange(fn, segment.first, segment.second, lo, hi);
            const bool rawSupported = pattern.EvaluateRawRange(segment.first, segment.second, rawLo, rawHi);
            ++queries;
            if(supported != example.supported || rawSupported != example.supported)
            {
                std::cerr << example.name << ": unexpected range eligibility\n";
                return 1;
            }
            if(!supported)
                continue;
            for(int j = 0; j <= 64; ++j)
            {
                const double t = j / 64.0;
                const Vector3d point = segment.first * (1.0 - t) + segment.second * t;
                context.SetLocal(0, point.x());
                context.SetLocal(1, point.y());
                context.SetLocal(2, point.z());
                const double value = POVFPU_RunScalar(&context, fn);
                const double raw = value > 1.0 ? std::fmod(value, 1.0) : value;
                ++samples;
                if(!Contains(lo, hi, value) || !Contains(rawLo, rawHi, raw))
                {
                    std::cerr << example.name << ": sample outside range: " << value << " in ["
                              << lo << ", " << hi << "], wrapped " << raw << " in ["
                              << rawLo << ", " << rawHi << "]\n";
                    return 1;
                }
            }
        }
    }
    std::cout << queries << " range queries; " << samples << " scalar samples; no containment failures\n";
}
