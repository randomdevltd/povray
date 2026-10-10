// SPDX-License-Identifier: AGPL-3.0-or-later

#include "parser/pov4.h"

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#if defined(__has_include)
#if __has_include(<charconv>)
#include <charconv>
#endif
#endif
#if !defined(__cpp_lib_to_chars)
#include <iomanip>
#include <locale>
#include <sstream>
#endif

#include "tree_sitter/api.h"

#include "base/fileinputoutput.h"
#include "base/fileutil.h"
#include "base/mathutil.h"
#include "base/stringutilities.h"
#include "core/math/matrix.h"
#include "core/scene/scenedata.h"
#include "parser/parser.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <ucontext.h>
#endif

#include "base/povdebug.h"

extern "C" const TSLanguage* tree_sitter_pov4();

namespace pov_parser
{

using namespace pov_base;
using namespace pov;

bool IsPov4File(const UCS2String& fileName)
{
    size_t dot = fileName.find_last_of(u'.');
    if (dot == UCS2String::npos)
        return false;
    std::string ext = UCS2toSysString(fileName.substr(dot));
    return (pov_stricmp(ext.c_str(), ".pov4") == 0) || (pov_stricmp(ext.c_str(), ".inc4") == 0);
}

namespace
{

inline bool FTrue(double f) { return std::fabs(f) > EPSILON; }

const int kMaxDepth = 100000;
const int kMaxCalls = 10000;
const int kMaxIncludeDepth = 100;
const int kMaxDims = 5;
const size_t kMaxArrayBytes = size_t(256) << 20;
const size_t kPendingFlush = size_t(1) << 20;
const size_t kEvaluatorStack = size_t(256) << 20;

enum class NK : unsigned char
{
    File, Let, Global, Assign, FnDef, Params, Param, If, While, For, ForIn, Break, Continue, Return, Include,
    Keyword, Comma, Spread, Number, String, Ident, Builtin, True, False, Null, Vector, Array, Dict, Pair,
    Block, FunctionBlock, Cond, Binary, Unary, Lambda, Colour, Channel, Call, Index, Member, Body
};

enum Op : unsigned char { OpOr, OpAnd, OpEq, OpNe, OpLt, OpLe, OpGt, OpGe, OpAdd, OpSub, OpMul, OpDiv, OpNeg, OpPlus, OpNot };

enum BI : unsigned char
{
    B_x, B_y, B_z, B_t, B_u, B_v, B_pi, B_tau, B_clock, B_clock_on, B_yes, B_no, B_on, B_off, B_version, B_now,
    B_abs, B_acos, B_acosh, B_asin, B_asinh, B_atan, B_atan2, B_atanh, B_ceil, B_cos, B_cosh, B_degrees, B_div,
    B_exp, B_floor, B_int, B_ln, B_log, B_max, B_min, B_mod, B_pow, B_radians, B_select, B_sin, B_sinh, B_sqrt,
    B_sqr, B_tan, B_tanh, B_vdot, B_vlength, B_vcross, B_vnormalize, B_vrotate, B_vaxis_rotate, B_strlen,
    B_strcmp, B_asc, B_val, B_concat, B_chr, B_bitwise_and, B_bitwise_or, B_bitwise_xor, B_dimensions,
    B_dimension_size, B_seed, B_rand, B_len, B_range, B_map, B_filter, B_push, B_keys, B_defined, B_debug,
    B_warning, B_error, B_array, B_classic
};

const std::unordered_map<std::string, BI>& BuiltinNames()
{
    static const std::unordered_map<std::string, BI> names = {
        {"x", B_x}, {"y", B_y}, {"z", B_z}, {"t", B_t}, {"u", B_u}, {"v", B_v}, {"pi", B_pi}, {"tau", B_tau},
        {"clock", B_clock}, {"clock_on", B_clock_on}, {"yes", B_yes}, {"no", B_no}, {"on", B_on}, {"off", B_off},
        {"version", B_version}, {"now", B_now}, {"abs", B_abs}, {"acos", B_acos}, {"acosh", B_acosh},
        {"asin", B_asin}, {"asinh", B_asinh}, {"atan", B_atan}, {"atan2", B_atan2}, {"atanh", B_atanh},
        {"ceil", B_ceil}, {"cos", B_cos}, {"cosh", B_cosh}, {"degrees", B_degrees}, {"div", B_div},
        {"exp", B_exp}, {"floor", B_floor}, {"int", B_int}, {"ln", B_ln}, {"log", B_log}, {"max", B_max},
        {"min", B_min}, {"mod", B_mod}, {"pow", B_pow}, {"radians", B_radians}, {"select", B_select},
        {"sin", B_sin}, {"sinh", B_sinh}, {"sqrt", B_sqrt}, {"sqr", B_sqr}, {"tan", B_tan}, {"tanh", B_tanh},
        {"vdot", B_vdot}, {"vlength", B_vlength}, {"vcross", B_vcross}, {"vnormalize", B_vnormalize},
        {"vrotate", B_vrotate}, {"vaxis_rotate", B_vaxis_rotate}, {"strlen", B_strlen}, {"strcmp", B_strcmp},
        {"asc", B_asc}, {"val", B_val}, {"concat", B_concat}, {"chr", B_chr}, {"bitwise_and", B_bitwise_and},
        {"bitwise_or", B_bitwise_or}, {"bitwise_xor", B_bitwise_xor}, {"dimensions", B_dimensions},
        {"dimension_size", B_dimension_size}, {"seed", B_seed}, {"rand", B_rand}, {"len", B_len},
        {"range", B_range}, {"map", B_map}, {"filter", B_filter}, {"push", B_push}, {"keys", B_keys},
        {"defined", B_defined}, {"debug", B_debug}, {"warning", B_warning}, {"error", B_error}, {"array", B_array},
    };
    return names;
}

const char* const kObjectKeywords[] = {
    "bicubic_patch", "blob", "box", "composite", "cone", "cubic", "cylinder", "difference", "disc", "height_field",
    "intersection", "isosurface", "isosurface_mesh", "julia_fractal", "lathe", "lemon", "light_group",
    "light_source", "merge", "mesh", "mesh2", "object", "ovus", "parametric", "plane", "poly", "polygon",
    "polynomial", "portal", "prism", "quadric", "quartic", "skein", "skein_mesh", "smooth_triangle", "sor",
    "sphere", "sphere_sweep", "superellipsoid", "text", "torus", "triangle", "union",
};
const char* const kChildKeywords[] = {
    "composite", "difference", "intersection", "light_group", "merge", "union",
};
const char* const kSceneKeywords[] = {
    "background", "camera", "default", "fog", "global_settings", "media", "photons", "radiosity", "rainbow",
    "sky_sphere",
};
const char* const kWrapKeywords[] = {
    "camera", "color_map", "density", "density_map", "finish", "fog", "interior", "material", "media", "normal",
    "normal_map", "pigment", "pigment_map", "rainbow", "sky_sphere", "slope_map", "texture", "texture_map",
    "transform",
};
const char* const kUndeclarable[] = {
    "background", "default", "global_settings", "interior_texture", "photons", "radiosity",
};

bool InList(const char* const* list, size_t n, const std::string& word)
{
    for (size_t i = 0; i < n; ++i)
        if (word == list[i])
            return true;
    return false;
}
#define IN_LIST(list, word) InList(list, sizeof(list) / sizeof(list[0]), word)

std::string Normalised(const std::string& keyword)
{
    if (keyword == "colour_map")
        return "color_map";
    if (keyword == "interior_texture")
        return "texture";
    return keyword;
}

int ComponentIndex(const std::string& m)
{
    return (m == "x" || m == "u" || m == "red") ? 0 : (m == "y" || m == "v" || m == "green") ? 1 :
           (m == "z" || m == "blue") ? 2 : (m == "t" || m == "filter") ? 3 : (m == "transmit") ? 4 : -1;
}

std::string WrapOfKeyword(const std::string& keyword)
{
    std::string k = Normalised(keyword);
    if (IN_LIST(kWrapKeywords, k))
        return k;
    if ((k == "function") || (k == "spline"))
        return std::string();
    return "object";
}

std::string WrapOfToken(TokenId token)
{
    switch (token)
    {
        case OBJECT_ID_TOKEN:       return "object";
        case TEXTURE_ID_TOKEN:      return "texture";
        case PIGMENT_ID_TOKEN:      return "pigment";
        case NORMAL_ID_TOKEN:       return "normal";
        case FINISH_ID_TOKEN:       return "finish";
        case INTERIOR_ID_TOKEN:     return "interior";
        case MEDIA_ID_TOKEN:        return "media";
        case MATERIAL_ID_TOKEN:     return "material";
        case TRANSFORM_ID_TOKEN:    return "transform";
        case CAMERA_ID_TOKEN:       return "camera";
        case FOG_ID_TOKEN:          return "fog";
        case RAINBOW_ID_TOKEN:      return "rainbow";
        case SKYSPHERE_ID_TOKEN:    return "sky_sphere";
        case DENSITY_ID_TOKEN:      return "density";
        case COLOUR_MAP_ID_TOKEN:   return "color_map";
        case PIGMENT_MAP_ID_TOKEN:  return "pigment_map";
        case NORMAL_MAP_ID_TOKEN:   return "normal_map";
        case SLOPE_MAP_ID_TOKEN:    return "slope_map";
        case TEXTURE_MAP_ID_TOKEN:  return "texture_map";
        case DENSITY_MAP_ID_TOKEN:  return "density_map";
        default:                    return std::string();
    }
}

bool ParseNumber(const std::string& text, double& d)
{
#if defined(__cpp_lib_to_chars)
    const char* end = text.data() + text.size();
    std::from_chars_result r = std::from_chars(text.data(), end, d);
    if (r.ec == std::errc::result_out_of_range)
    {
        size_t e = text.find_first_of("eE");
        bool tiny = (e != std::string::npos) ? (text[e + 1] == '-') : (text.find_first_of("123456789") > text.find('.'));
        d = tiny ? 0.0 : HUGE_VAL;
    }
    return (r.ptr == end) && (r.ec != std::errc::invalid_argument);
#else
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> d;
    return !in.fail();
#endif
}

void AppendNumber(std::string& out, double d)
{
    if (std::isnan(d))
        throw POV_EXCEPTION(kParseErr, "Not a number.");
    if (std::isinf(d))
    {
        out += (d > 0) ? "1e400" : "-1e400";
        return;
    }
#if defined(__cpp_lib_to_chars)
    char buffer[32];
    out.append(buffer, std::to_chars(buffer, buffer + sizeof(buffer), d).ptr);
#else
    std::ostringstream format;
    format.imbue(std::locale::classic());
    format << std::setprecision(17) << d;
    out += format.str();
#endif
}

std::string UCS2toUTF8(const UCS2String& s)
{
    std::string out;
    for (UCS2 c : s)
    {
        if (c < 0x80)
            out += char(c);
        else if (c < 0x800)
        {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        }
        else
        {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string EscapeString(const UCS2String& s)
{
    std::string out = "\"";
    char buf[8];
    for (UCS2 c : s)
    {
        if ((c == u'"') || (c == u'\\'))
        {
            out += '\\';
            out += char(c);
        }
        else if (((c >= 0x20) && (c < 0x7F)) || ((c >= 0x80) && ((c < 0xD800) || (c > 0xDFFF))))
            out += UCS2toUTF8(UCS2String(1, c));
        else
        {
            std::snprintf(buf, sizeof(buf), "\\u%04X", unsigned(c));
            out += buf;
        }
    }
    return out + "\"";
}

UCS2String DecodeString(const std::string& raw)
{
    UCS2String out;
    auto i = raw.cbegin() + 1;
    auto end = raw.cend() - 1;
    while (i != end)
    {
        UCS4 c;
        if (*i == '\\')
        {
            ++i;
            char e = *i++;
            switch (e)
            {
                case 'a': c = 0x07; break;
                case 'b': c = 0x08; break;
                case 't': c = 0x09; break;
                case 'n': c = 0x0A; break;
                case 'v': c = 0x0B; break;
                case 'f': c = 0x0C; break;
                case 'r': c = 0x0D; break;
                case 'u':
                case 'U':
                {
                    int digits = (e == 'u') ? 4 : 6;
                    c = 0;
                    for (int k = 0; (k < digits) && (i != end) && std::isxdigit(static_cast<unsigned char>(*i)); ++k, ++i)
                        c = c * 16 + UCS4(std::isdigit(static_cast<unsigned char>(*i)) ? (*i - '0') : (std::tolower(*i) - 'a' + 10));
                    break;
                }
                default: c = UCS4(static_cast<unsigned char>(e)); break;
            }
        }
        else if (static_cast<unsigned char>(*i) <= 0x7F)
            c = UCS4(*i++);
        else if (!UCS::DecodeUTF8Sequence(c, i, end))
            c = UCS::kReplacementCharacter;
        out += UCS2(c);
    }
    return out;
}

struct Node
{
    NK kind;
    unsigned char op = 0;
    unsigned file = 0;
    int sym = -1;
    unsigned line = 0;
    unsigned column = 0;
    double num = 0.0;
    std::string text;
    UCS2String str;
    std::vector<const Node*> kids;
    const Node* a = nullptr;
    const Node* b = nullptr;
    const Node* c = nullptr;
    const Node* d = nullptr;
    mutable const void* cacheScope = nullptr;
    mutable void* cacheSlot = nullptr;
    mutable unsigned long cacheEpoch = 0;
};

struct Scope;
struct Item;

struct PValue
{
    enum Type : unsigned char { Null, Num, Vec, Str, Arr, Dict, Fn, Builtin, Frag, Handle };
    Type type = Null;
    unsigned char size = 0;
    bool colour = false;
    double v[5];
    std::shared_ptr<void> ref;

    static PValue Number(double d) { PValue r; r.type = Num; r.size = 1; r.v[0] = d; return r; }
    static PValue Vector(const double* e, int n) { PValue r; r.type = Vec; r.size = (unsigned char)n; for (int i = 0; i < n; ++i) r.v[i] = e[i]; return r; }
    bool IsNumeric() const { return (type == Num) || (type == Vec); }
    template<typename T> T& As() const { return *static_cast<T*>(ref.get()); }
};

using PValues = std::vector<PValue>;

const size_t kMaxArray = kMaxArrayBytes / sizeof(PValue);

struct StrData { UCS2String s; std::string raw; };

struct DictData
{
    std::vector<std::pair<std::string, PValue>> items;
    std::unordered_map<std::string, size_t> index;
    PValue* Find(const std::string& key) { auto i = index.find(key); return (i == index.end()) ? nullptr : &items[i->second].second; }
    void Set(const std::string& key, const PValue& v)
    {
        auto i = index.find(key);
        if (i != index.end())
            items[i->second].second = v;
        else
        {
            index.emplace(key, items.size());
            items.emplace_back(key, v);
        }
    }
};

struct Closure
{
    const Node* params;
    const Node* body;
    bool expressionBody;
    std::shared_ptr<Scope> env;
};

struct Item
{
    enum Kind : unsigned char { Val, Keyword, Block, Bracket, Raw, Comma };
    Kind kind;
    PValue value;
    std::string text;
    std::string name;
    const Node* at;
    std::vector<std::shared_ptr<void>> refs;
    Item(const PValue& v, const Node* n) : kind(Val), value(v), at(n) {}
    Item(Kind k, std::string t, std::string nm, const Node* n) : kind(k), text(std::move(t)), name(std::move(nm)), at(n) {}
};

using Items = std::vector<Item>;

struct Scope
{
    std::shared_ptr<Scope> parent;
    std::unordered_map<int, PValue> vars;
    explicit Scope(std::shared_ptr<Scope> p) : parent(std::move(p)) {}
};

using Refs = std::vector<std::shared_ptr<void>>;

void Unlink(PValues& c, Refs& out) { for (PValue& e : c) if (e.ref) out.push_back(std::move(e.ref)); }
void Unlink(DictData& c, Refs& out) { for (auto& kv : c.items) if (kv.second.ref) out.push_back(std::move(kv.second.ref)); }
void Unlink(Items& c, Refs& out)
{
    for (Item& item : c)
    {
        if (item.value.ref)
            out.push_back(std::move(item.value.ref));
        std::move(item.refs.begin(), item.refs.end(), std::back_inserter(out));
    }
}

struct Graveyard { Refs refs; bool active = false; };
thread_local Graveyard tGraveyard;

template<typename T>
struct Reaper
{
    void operator()(T* p) const
    {
        Graveyard& g = tGraveyard;
        Unlink(*p, g.refs);
        delete p;
        if (g.active)
            return;
        g.active = true;
        while (!g.refs.empty())
        {
            std::shared_ptr<void> last = std::move(g.refs.back());
            g.refs.pop_back();
            last.reset();
        }
        g.active = false;
    }
};

template<typename T, typename... A>
std::shared_ptr<T> NewContainer(A&&... args) { return std::shared_ptr<T>(new T(std::forward<A>(args)...), Reaper<T>()); }

}
// end of anonymous namespace

class Pov4Evaluator final
{
public:
    explicit Pov4Evaluator(Parser& parser);
    ~Pov4Evaluator();
    void Run();
    void Release(const std::string& name) { if (!mClosing) mDead.push_back(name); }

private:
    enum class Flow { Normal, Break, Continue, Return };

    struct HandleData
    {
        std::string name;
        std::string wrap;
        TokenId token;
        bool owned;
        Pov4Evaluator* owner;
        ~HandleData() { if (owned) owner->Release(name); }
    };

    struct SyncData
    {
        Pov4Evaluator* owner;
        std::vector<int> syms;
        explicit SyncData(Pov4Evaluator* ev) : owner(ev) {}
        ~SyncData() { owner->Settle(this); }
    };

    struct DepthGuard
    {
        int& depth;
        DepthGuard(Pov4Evaluator& ev, const Node* at) : DepthGuard(ev, ev.mDepth, kMaxDepth, "Nesting", at) {}
        DepthGuard(Pov4Evaluator& ev, int& d, int limit, const char* what, const Node* at) : depth(d)
        {
            if (++depth > limit)
            {
                --depth;
                ev.Fail(at, "%s deeper than %d levels.", what, limit);
            }
        }
        ~DepthGuard() { --depth; }
    };

    struct Output
    {
        Pov4Evaluator& ev;
        explicit Output(Pov4Evaluator& e) : ev(e) {}
        virtual ~Output() = default;
        virtual void Put(const Item& item, bool spliced) = 0;
        virtual bool IsBlock() const { return false; }
        virtual bool IsScene() const { return false; }
    };

    struct TextOutput final : Output
    {
        std::string buf;
        std::string context;
        std::string lastKeyword;
        unsigned line;
        unsigned file;
        bool lastValue = false;
        TextOutput(Pov4Evaluator& e, const std::string& ctx, const Node* at) : Output(e), context(ctx), line(at->line), file(at->file) {}
        bool IsBlock() const override { return true; }
        void Append(const std::string& text, const Node* at);
        void Put(const Item& item, bool spliced) override;
        void PutValue(const PValue& v, const Node* at, bool spliced);
    };

    struct SceneOutput final : Output
    {
        explicit SceneOutput(Pov4Evaluator& e) : Output(e) {}
        bool IsScene() const override { return true; }
        void Put(const Item& item, bool spliced) override;
    };

    struct ListOutput final : Output
    {
        Items items;
        explicit ListOutput(Pov4Evaluator& e) : Output(e) {}
        void Put(const Item& item, bool spliced) override;
    };

    struct ArrayOutput final : Output
    {
        PValues values;
        explicit ArrayOutput(Pov4Evaluator& e) : Output(e) {}
        void Put(const Item& item, bool spliced) override;
    };

    struct Program
    {
        std::deque<Node> nodes;
        const Node* root = nullptr;
        std::string source;
    };

    Parser& mParser;
    SceneData& mScene;
    std::vector<std::unique_ptr<Program>> mPrograms;
    std::vector<UCS2String> mFiles;
    std::unordered_map<std::string, int> mSymbols;
    std::vector<std::string> mSymbolNames;
    std::unordered_set<std::string> mReserved;
    std::shared_ptr<Scope> mFileScope;
    std::shared_ptr<Scope> mScope;
    PValue mReturn;
    bool mReturnBare = false;
    std::vector<std::shared_ptr<void>> mPrinted;
    std::string mPending;
    std::vector<POV_LONG> mPendingLines;
    unsigned mPendingFile = 0;
    bool mPendingRaw = false;
    std::vector<std::string> mDead;
    std::unordered_map<int, std::string> mExported;
    std::unordered_set<int> mDirty;
    std::unordered_set<const void*> mSyncs;
    std::unordered_map<int, int> mInFlight;
    unsigned long mNextName = 0;
    std::unique_ptr<std::ofstream> mDump;
    bool mClosing = false;
    int mDepth = 0;
    int mCalls = 0;
    int mIncludeDepth = 0;
    unsigned long mSteps = 0;
    std::unordered_map<UCS2String, Program*> mProgramCache;
    unsigned long mEpoch = 1;
    std::vector<std::unique_ptr<PValues>> mArgPool;
    size_t mArgDepth = 0;

    [[noreturn]] void Fail(const Node* at, const char* format, ...);
    std::string TypeName(const PValue& v) const;
    int Intern(const std::string& name);

    Program& Load(const UCS2String& fileName, unsigned int fileType, const Node* at);
    const Node* Lower(Program& p, TSNode n);
    Node& NewNode(Program& p, NK kind, TSNode n);
    void LowerItems(Program& p, TSNode n, Node& into, const char* skipField);
    int CheckedName(Program& p, TSNode n);

    void Snippet(const std::string& text, std::vector<POV_LONG> lines, unsigned file);
    Item TextItem(Item::Kind kind, std::string text, std::string name, const Node* at, size_t mark);
    void Flush();
    void AppendPending(const std::string& text, const Node* at);
    std::string NewName() { return "__pov4_" + std::to_string(++mNextName); }
    PValue MakeHandle(const std::string& name, const std::string& wrap, TokenId token, bool owned);
    PValue FromClassic(SYM_ENTRY* entry, const std::string& name, bool owned);
    PValue ClassicEval(const std::string& expr, const Node* at);
    PValue DeclareBlock(const std::string& keyword, const std::string& text, const Node* at,
                        const std::vector<std::shared_ptr<void>>& refs = {});
    void CollectRefs(const PValue& v);
    bool ClassicLookup(const std::string& name, PValue& out);
    std::string ExportText(const PValue& v, const Node* at);
    bool ExportInto(std::string& out, const PValue& v, const Node* at);
    int ArrayShape(const PValues& a, int* sizes);
    bool ArrayText(std::string& out, const PValues& a, const std::function<bool(std::string&, const PValue&)>& element);
    void MarkDirty(const Node* target);
    void ExportBindings(const Node* at, Output* out, SyncData* sync);
    void ImportBindings();
    void Settle(SyncData* sync);
    void ClassicRan(const std::vector<std::shared_ptr<void>>& refs);

    Flow ExecItems(const std::vector<const Node*>& items, Output& out);
    Flow Exec(const Node* n, Output& out);
    void Emit(const Node* n, Output& out);
    PValue Eval(const Node* n);
    PValue EvalArray(const Node* n);
    PValue EvalDict(const Node* n);
    PValue EvalBinary(const Node* n);
    PValue EvalColour(const Node* n);
    std::string ColourText(const Node* n);
    PValue EvalIndex(const Node* n);
    PValue EvalMember(const Node* n);
    PValue Lookup(const Node* n);
    PValue* FindBinding(int sym);
    PValue* FindBinding(const Node* n);
    PValue& LRef(const Node* n);
    PValue& DictMember(PValue& base, const Node* n);
    void Assign(const Node* target, const PValue& v);
    void Bind(int sym, const PValue& v, bool global);

    std::string LowerBlock(const Node* n);
    std::string LowerFunction(const Node* n);
    void PrintFunctionExpr(std::string& out, const Node* n, const std::unordered_set<int>& params);

    PValue Call(const Node* n);
    PValue CallTrace(const Node* n);
    void CallEmit(const Node* n, Output& out);
    void EvalArgs(const Node* n, PValues& args);
    PValue CallValue(const PValue& callee, PValues& args, const Node* at);
    PValue CallClosure(const Closure& c, PValues& args, const Node* at);
    PValue CallBuiltin(BI id, const std::string& word, PValues& args, const Node* call, const Node* at);
    std::string ClassicCallText(const std::string& name, const PValues& args, const Node* at);

    PValue AsValue(const PValue& v, const Node* at);
    PValue ToValue(const Item& item);
    PValue Store(const PValue& v, const Node* at);
    void Print(std::string& out, const PValue& v, const Node* at, bool colourKeyword);
    double Float(const PValue& v, const Node* at, const char* what);
    Vector3d Vec3(const PValue& v, const Node* at);
    const UCS2String& Str(const PValue& v, const Node* at);
    bool Truth(const PValue& v, const Node* at);
    PValue MakeString(const UCS2String& s);
    PValue MakeArray(PValues&& values);
    void Splice(const PValue& v, Output& out, const Node* at);
};

Pov4Evaluator::Pov4Evaluator(Parser& parser) :
    mParser(parser),
    mScene(*parser.sceneData)
{
    for (int i = 0; Reserved_Words[i].Token_Name != nullptr; ++i)
        mReserved.insert(Reserved_Words[i].Token_Name);
    for (const char* w : {"let", "fn", "global", "return", "null", "in", "to", "step", "continue"})
        mReserved.insert(w);
    mFileScope = std::make_shared<Scope>(nullptr);
    mScope = mFileScope;
}

Pov4Evaluator::~Pov4Evaluator()
{
    mClosing = true;
    mReturn = PValue();
    mScope.reset();
    mFileScope.reset();
}

void Pov4Evaluator::Fail(const Node* at, const char* format, ...)
{
    char buffer[1024];
    va_list marker;
    va_start(marker, format);
    std::vsnprintf(buffer, sizeof(buffer), format, marker);
    va_end(marker);
    if (at == nullptr)
        mParser.Error("%s", buffer);
    UCS2String file = mFiles[at->file];
    mParser.Error(SourceInfo(file, SourcePosition(at->line, at->column, 0)), "%s:%u:%u: %s",
                  UCS2toSysString(file).c_str(), at->line, at->column, buffer);
    throw POV_EXCEPTION(kParseErr, buffer);
}

std::string Pov4Evaluator::TypeName(const PValue& v) const
{
    switch (v.type)
    {
        case PValue::Null:       return "null";
        case PValue::Num:        return "float";
        case PValue::Vec:        return (v.size == 5) ? "colour" : std::to_string(v.size) + "-vector";
        case PValue::Str:        return "string";
        case PValue::Arr:        return "array";
        case PValue::Dict:       return "dictionary";
        case PValue::Fn:         return "function";
        case PValue::Builtin:    return "built-in function";
        case PValue::Frag:       return "fragment of " + std::to_string(v.As<Items>().size()) + " items";
        case PValue::Handle:
        {
            const HandleData& h = v.As<HandleData>();
            return h.wrap.empty() ? "classic identifier" : h.wrap;
        }
    }
    return "value";
}

int Pov4Evaluator::Intern(const std::string& name)
{
    auto i = mSymbols.find(name);
    if (i != mSymbols.end())
        return i->second;
    mSymbolNames.push_back(name);
    return mSymbols[name] = int(mSymbolNames.size() - 1);
}

//------------------------------------------------------------------------------
// Concrete syntax tree to IR

namespace
{

std::string NodeText(const std::string& source, TSNode n)
{
    return source.substr(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
}

TSNode Field(TSNode n, const char* name)
{
    return ts_node_child_by_field_name(n, name, uint32_t(std::strlen(name)));
}

bool FirstError(TSNode n, TSNode& found)
{
    while (ts_node_has_error(n))
    {
        if (ts_node_is_error(n) || ts_node_is_missing(n))
        {
            found = n;
            return true;
        }
        uint32_t i = 0;
        while ((i < ts_node_child_count(n)) && !ts_node_has_error(ts_node_child(n, i)) && !ts_node_is_missing(ts_node_child(n, i)))
            ++i;
        if (i == ts_node_child_count(n))
        {
            found = n;
            return true;
        }
        n = ts_node_child(n, i);
    }
    return false;
}

TSNode FirstNamed(TSNode n)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i)
        if (std::strcmp(ts_node_type(ts_node_named_child(n, i)), "comment") != 0)
            return ts_node_named_child(n, i);
    return ts_node_named_child(n, 0);
}


const std::unordered_map<std::string, unsigned char>& Operators()
{
    static const std::unordered_map<std::string, unsigned char> ops = {
        {"||", OpOr}, {"&&", OpAnd}, {"==", OpEq}, {"!=", OpNe}, {"<", OpLt}, {"<=", OpLe}, {">", OpGt},
        {">=", OpGe}, {"+", OpAdd}, {"-", OpSub}, {"*", OpMul}, {"/", OpDiv},
    };
    return ops;
}

}
// end of anonymous namespace

Node& Pov4Evaluator::NewNode(Program& p, NK kind, TSNode n)
{
    p.nodes.emplace_back();
    Node& node = p.nodes.back();
    node.kind = kind;
    node.file = unsigned(mFiles.size() - 1);
    TSPoint start = ts_node_start_point(n);
    node.line = start.row + 1;
    node.column = start.column + 1;
    return node;
}

int Pov4Evaluator::CheckedName(Program& p, TSNode n)
{
    std::string name = NodeText(p.source, n);
    if (mReserved.count(name) != 0)
    {
        Node& at = NewNode(p, NK::Ident, n);
        Fail(&at, "'%s' is a reserved word and cannot be used as a name.", name.c_str());
    }
    if (name.compare(0, 7, "__pov4_") == 0)
    {
        Node& at = NewNode(p, NK::Ident, n);
        Fail(&at, "Names starting with '__pov4_' are reserved.");
    }
    return Intern(name);
}

void Pov4Evaluator::LowerItems(Program& p, TSNode n, Node& into, const char* skipField)
{
    for (uint32_t i = 0; i < ts_node_child_count(n); ++i)
    {
        TSNode child = ts_node_child(n, i);
        const char* field = ts_node_field_name_for_child(n, i);
        if ((skipField != nullptr) && (field != nullptr) && (std::strcmp(field, skipField) == 0))
            continue;
        if (!ts_node_is_named(child))
        {
            if (std::strcmp(ts_node_type(child), ",") == 0)
                into.kids.push_back(&NewNode(p, NK::Comma, child));
            continue;
        }
        if (std::strcmp(ts_node_type(child), "comment") == 0)
            continue;
        into.kids.push_back(Lower(p, child));
    }
}

const Node* Pov4Evaluator::Lower(Program& p, TSNode n)
{
    if (mDepth >= kMaxDepth)
        Fail(&NewNode(p, NK::Null, n), "Nesting deeper than %d levels.", kMaxDepth);
    struct Count { int& depth; ~Count() { --depth; } } count{++mDepth};
    const std::string type = ts_node_type(n);
    auto named = [&](const char* field) -> const Node* {
        TSNode f = Field(n, field);
        return ts_node_is_null(f) ? nullptr : Lower(p, f);
    };

    if (type == "parenthesized_expression")
    {
        for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i)
            if (std::strcmp(ts_node_type(ts_node_named_child(n, i)), "comment") != 0)
                return Lower(p, ts_node_named_child(n, i));
    }
    if ((type == "source_file") || (type == "body") || (type == "array") || (type == "vector") || (type == "dictionary"))
    {
        NK kind = (type == "source_file") ? NK::File : (type == "body") ? NK::Body : (type == "array") ? NK::Array :
                  (type == "vector") ? NK::Vector : NK::Dict;
        Node& node = NewNode(p, kind, n);
        LowerItems(p, n, node, nullptr);
        if ((kind == NK::Vector) || (kind == NK::Dict))
            node.kids.erase(std::remove_if(node.kids.begin(), node.kids.end(),
                                           [](const Node* k) { return k->kind == NK::Comma; }), node.kids.end());
        return &node;
    }
    if (type == "block")
    {
        Node& node = NewNode(p, NK::Block, n);
        node.text = NodeText(p.source, Field(n, "name"));
        LowerItems(p, n, node, "name");
        return &node;
    }
    if (type == "function_block")
    {
        Node& node = NewNode(p, NK::FunctionBlock, n);
        TSNode params = Field(n, "parameters");
        if (!ts_node_is_null(params))
        {
            Node& pn = NewNode(p, NK::Params, params);
            for (uint32_t i = 0; i < ts_node_named_child_count(params); ++i)
            {
                TSNode c = ts_node_named_child(params, i);
                if (std::strcmp(ts_node_type(c), "comment") == 0)
                    continue;
                Node& param = NewNode(p, NK::Param, c);
                param.text = NodeText(p.source, c);
                param.sym = Intern(param.text);
                pn.kids.push_back(&param);
            }
            node.a = &pn;
        }
        node.b = named("width");
        node.c = named("height");
        bool inBody = false;
        for (uint32_t i = 0; i < ts_node_child_count(n); ++i)
        {
            TSNode child = ts_node_child(n, i);
            const char* t = ts_node_type(child);
            if (!ts_node_is_named(child))
            {
                inBody = inBody || (std::strcmp(t, "{") == 0);
                if (inBody && (std::strcmp(t, ",") == 0))
                    node.kids.push_back(&NewNode(p, NK::Comma, child));
                continue;
            }
            if (inBody && (std::strcmp(t, "comment") != 0))
                node.kids.push_back(Lower(p, child));
        }
        return &node;
    }
    if ((type == "keyword") || (type == "identifier") || (type == "builtin"))
    {
        NK kind = (type == "keyword") ? NK::Keyword : (type == "identifier") ? NK::Ident : NK::Builtin;
        Node& node = NewNode(p, kind, n);
        node.text = NodeText(p.source, n);
        if (kind == NK::Ident)
            node.sym = CheckedName(p, n);
        else if (kind == NK::Builtin)
        {
            auto i = BuiltinNames().find(node.text);
            node.op = (i == BuiltinNames().end()) ? B_classic : i->second;
        }
        return &node;
    }
    if (type == "number")
    {
        Node& node = NewNode(p, NK::Number, n);
        node.text = NodeText(p.source, n);
        if (!ParseNumber(node.text, node.num))
            Fail(&node, "Malformed number '%s'.", node.text.c_str());
        return &node;
    }
    if (type == "string")
    {
        Node& node = NewNode(p, NK::String, n);
        node.text = NodeText(p.source, n);
        node.str = DecodeString(node.text);
        return &node;
    }
    if ((type == "true") || (type == "false") || (type == "null"))
        return &NewNode(p, (type == "true") ? NK::True : (type == "false") ? NK::False : NK::Null, n);
    if ((type == "let_statement") || (type == "global_statement") || (type == "assignment"))
    {
        Node& node = NewNode(p, (type == "let_statement") ? NK::Let : (type == "global_statement") ? NK::Global : NK::Assign, n);
        if (node.kind == NK::Assign)
            node.c = named("target");
        else
            node.sym = CheckedName(p, Field(n, "name"));
        for (uint32_t i = 0; i < ts_node_child_count(n); ++i)
        {
            const char* field = ts_node_field_name_for_child(n, i);
            if ((field != nullptr) && (std::strcmp(field, "value") == 0))
                node.kids.push_back(Lower(p, ts_node_child(n, i)));
        }
        node.a = node.kids.empty() ? nullptr : node.kids.front();
        if (node.kids.size() > 1)
            for (const Node* k : node.kids)
                if (k->kind != NK::Block)
                    Fail(k, "Only blocks can follow one another in a binding.");
        return &node;
    }
    if ((type == "parameters") || (type == "lambda") || (type == "function_definition"))
    {
        auto lowerParams = [&](TSNode params) -> const Node* {
            Node& pn = NewNode(p, NK::Params, params);
            if (std::strcmp(ts_node_type(params), "identifier") == 0)
            {
                Node& param = NewNode(p, NK::Param, params);
                param.sym = CheckedName(p, params);
                pn.kids.push_back(&param);
                return &pn;
            }
            for (uint32_t i = 0; i < ts_node_named_child_count(params); ++i)
            {
                TSNode c = ts_node_named_child(params, i);
                if (std::strcmp(ts_node_type(c), "parameter") != 0)
                    continue;
                Node& param = NewNode(p, NK::Param, c);
                param.sym = CheckedName(p, Field(c, "name"));
                TSNode def = Field(c, "default");
                if (!ts_node_is_null(def))
                    param.a = Lower(p, def);
                pn.kids.push_back(&param);
            }
            return &pn;
        };
        if (type == "parameters")
            return lowerParams(n);
        Node& node = NewNode(p, (type == "lambda") ? NK::Lambda : NK::FnDef, n);
        if (type == "function_definition")
            node.sym = CheckedName(p, Field(n, "name"));
        node.a = lowerParams(Field(n, "parameters"));
        node.b = named("body");
        node.op = (node.b->kind != NK::Body);
        return &node;
    }
    if (type == "if_statement")
    {
        Node& node = NewNode(p, NK::If, n);
        node.a = named("condition");
        node.b = named("consequence");
        node.c = named("alternative");
        return &node;
    }
    if (type == "while_statement")
    {
        Node& node = NewNode(p, NK::While, n);
        node.a = named("condition");
        node.b = named("body");
        return &node;
    }
    if (type == "for_statement")
    {
        TSNode iterable = Field(n, "iterable");
        Node& node = NewNode(p, ts_node_is_null(iterable) ? NK::For : NK::ForIn, n);
        node.sym = CheckedName(p, Field(n, "variable"));
        if (node.kind == NK::ForIn)
            node.a = Lower(p, iterable);
        else
        {
            node.a = named("start");
            node.b = named("end");
            node.c = named("step");
        }
        node.d = named("body");
        return &node;
    }
    if ((type == "break_statement") || (type == "continue_statement"))
        return &NewNode(p, (type == "break_statement") ? NK::Break : NK::Continue, n);
    if ((type == "return_statement") || (type == "include_statement") || (type == "spread"))
    {
        Node& node = NewNode(p, (type == "return_statement") ? NK::Return : (type == "spread") ? NK::Spread : NK::Include, n);
        if (type == "spread")
            node.a = Lower(p, FirstNamed(n));
        else
            node.a = named((type == "return_statement") ? "value" : "file");
        return &node;
    }
    if (type == "pair")
    {
        Node& node = NewNode(p, NK::Pair, n);
        TSNode key = Field(n, "key");
        if (ts_node_is_null(key))
            node.b = named("computed");
        else
            node.text = NodeText(p.source, key);
        if (!ts_node_is_null(key) && (std::strcmp(ts_node_type(key), "string") == 0))
            node.text = UCS2toUTF8(DecodeString(node.text));
        node.a = named("value");
        return &node;
    }
    if (type == "conditional_expression")
    {
        Node& node = NewNode(p, NK::Cond, n);
        node.a = named("condition");
        node.b = named("consequence");
        node.c = named("alternative");
        return &node;
    }
    if ((type == "binary_expression") || (type == "comparison_expression") || (type == "unary_expression"))
    {
        bool binary = (type != "unary_expression");
        Node& node = NewNode(p, binary ? NK::Binary : NK::Unary, n);
        std::string op = NodeText(p.source, Field(n, "operator"));
        if (binary)
        {
            node.op = Operators().at(op);
            node.a = named("left");
            node.b = named("right");
        }
        else
        {
            node.op = (op == "-") ? OpNeg : (op == "+") ? OpPlus : OpNot;
            node.a = named("operand");
        }
        return &node;
    }
    if ((type == "colour_expression") || (type == "channel"))
    {
        bool channel = (type == "channel");
        Node& node = NewNode(p, channel ? NK::Channel : NK::Colour, n);
        TSNode op = Field(n, channel ? "name" : "operator");
        if (!ts_node_is_null(op))
            node.text = NodeText(p.source, op);
        node.a = named("value");
        if (!channel)
            for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i)
                if (std::strcmp(ts_node_type(ts_node_named_child(n, i)), "channel") == 0)
                    node.kids.push_back(Lower(p, ts_node_named_child(n, i)));
        return &node;
    }
    if (type == "call_expression")
    {
        Node& node = NewNode(p, NK::Call, n);
        node.a = named("function");
        LowerItems(p, Field(n, "arguments"), node, nullptr);
        node.kids.erase(std::remove_if(node.kids.begin(), node.kids.end(),
                                       [](const Node* k) { return k->kind == NK::Comma; }), node.kids.end());
        return &node;
    }
    if (type == "index_expression")
    {
        Node& node = NewNode(p, NK::Index, n);
        node.a = named("object");
        node.b = named("index");
        return &node;
    }
    if (type == "member_expression")
    {
        Node& node = NewNode(p, NK::Member, n);
        node.a = named("object");
        node.text = NodeText(p.source, Field(n, "member"));
        return &node;
    }
    Node& node = NewNode(p, NK::Null, n);
    Fail(&node, "Unexpected '%s' in a 4.0 scene.", type.c_str());
}

Pov4Evaluator::Program& Pov4Evaluator::Load(const UCS2String& fileName, unsigned int fileType, const Node* at)
{
    UCS2String actual;
    std::shared_ptr<IStream> stream = mParser.Locate_File(fileName, fileType, actual, true);
    if (stream == nullptr)
        Fail(at, "Cannot open file '%s'.", UCS2toSysString(fileName).c_str());
    auto cached = mProgramCache.find(actual);
    if (cached != mProgramCache.end())
        return *cached->second;
    mPrograms.emplace_back(new Program());
    Program& p = *mPrograms.back();
    mProgramCache[actual] = &p;
    char buffer[65536];
    size_t count;
    while ((count = stream->readUpTo(buffer, sizeof(buffer))) > 0)
        p.source.append(buffer, count);
    if ((p.source.size() >= 3) && (p.source.compare(0, 3, "\xEF\xBB\xBF") == 0))
        p.source.replace(0, 3, "   ");
    mFiles.push_back(fileName);

    std::unique_ptr<TSParser, void(*)(TSParser*)> parser(ts_parser_new(), ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_pov4()))
        Fail(at, "Cannot initialise the 4.0 parser.");
    std::unique_ptr<TSTree, void(*)(TSTree*)> tree(ts_parser_parse_string(parser.get(), nullptr, p.source.data(),
                                                                         uint32_t(p.source.size())), ts_tree_delete);
    if (!tree)
        Fail(at, "Cannot parse '%s'.", UCS2toSysString(fileName).c_str());
    TSNode root = ts_tree_root_node(tree.get());
    TSNode error;
    if (FirstError(root, error))
    {
        Node& node = NewNode(p, NK::Null, error);
        if (ts_node_is_missing(error))
            Fail(&node, "Syntax error: missing '%s'.", ts_node_type(error));
        std::string text = NodeText(p.source, error).substr(0, 40);
        size_t newline = text.find('\n');
        Fail(&node, "Syntax error at '%s'.", text.substr(0, newline).c_str());
    }
    p.root = Lower(p, root);
    return p;
}

//------------------------------------------------------------------------------
// Bridge to the classic parser

void Pov4Evaluator::Snippet(const std::string& text, std::vector<POV_LONG> lines, unsigned file)
{
    if (mDump)
    {
        *mDump << text;
        if (!text.empty() && (text.back() != '\n'))
            *mDump << '\n';
        mDump->flush();
    }
    mParser.Parse_Snippet(text, (file < mFiles.size()) ? mFiles[file] : mScene.inputFile, std::move(lines));
}

void Pov4Evaluator::Flush()
{
    if (!mPending.empty())
    {
        std::string text;
        text.swap(mPending);
        mPendingRaw = false;
        Snippet(text, mPendingLines, mPendingFile);
    }
    if (!mDead.empty())
    {
        SymbolTable* global = mParser.mSymbolStack.GetGlobalTable();
        for (const std::string& name : mDead)
            if (global->Find_Symbol(name.c_str()) != nullptr)
                global->Remove_Symbol(name.c_str(), false, nullptr, 0);
        mDead.clear();
    }
}

void Pov4Evaluator::AppendPending(const std::string& text, const Node* at)
{
    unsigned file = (at != nullptr) ? at->file : mPendingFile;
    if (!mPending.empty() && ((file != mPendingFile) || (mPending.size() > kPendingFlush)))
        Flush();
    POV_LONG line = (at != nullptr) ? POV_LONG(at->line) : mPendingLines.empty() ? 1 : mPendingLines.back();
    if (mPending.empty())
    {
        mPendingFile = file;
        mPendingLines.assign(1, line);
    }
    else if (line != mPendingLines.back())
    {
        mPending += '\n';
        mPendingLines.push_back(line);
    }
    else
        mPending += ' ';
    mPending += text;
    for (char c : text)
        if (c == '\n')
            mPendingLines.push_back(++line);
}

Item Pov4Evaluator::TextItem(Item::Kind kind, std::string text, std::string name, const Node* at, size_t mark)
{
    Item item(kind, std::move(text), std::move(name), at);
    item.refs.assign(mPrinted.begin() + mark, mPrinted.end());
    mPrinted.resize(mark);
    return item;
}

PValue Pov4Evaluator::MakeHandle(const std::string& name, const std::string& wrap, TokenId token, bool owned)
{
    PValue v;
    v.type = PValue::Handle;
    auto h = std::make_shared<HandleData>();
    h->name = name;
    h->wrap = wrap;
    h->token = token;
    h->owned = owned;
    h->owner = this;
    v.ref = h;
    return v;
}

PValue Pov4Evaluator::FromClassic(SYM_ENTRY* entry, const std::string& name, bool owned)
{
    double e[5];
    switch (entry->Token_Number)
    {
        case FLOAT_ID_TOKEN:
            return PValue::Number(*reinterpret_cast<DBL*>(entry->Data));
        case VECTOR_ID_TOKEN:
        {
            const Vector3d& vec = *reinterpret_cast<Vector3d*>(entry->Data);
            e[0] = vec[X]; e[1] = vec[Y]; e[2] = vec[Z];
            return PValue::Vector(e, 3);
        }
        case UV_ID_TOKEN:
        {
            const Vector2d& vec = *reinterpret_cast<Vector2d*>(entry->Data);
            e[0] = vec[U]; e[1] = vec[V];
            return PValue::Vector(e, 2);
        }
        case VECTOR_4D_ID_TOKEN:
            return PValue::Vector(reinterpret_cast<DBL*>(entry->Data), 4);
        case COLOUR_ID_TOKEN:
        {
            reinterpret_cast<RGBFTColour*>(entry->Data)->Get(e, 5);
            PValue colour = PValue::Vector(e, 5);
            colour.colour = true;
            return colour;
        }
        case STRING_ID_TOKEN:
        {
            PValue v;
            v.type = PValue::Str;
            auto s = std::make_shared<StrData>();
            s->s = UCS2String(reinterpret_cast<UCS2*>(entry->Data));
            v.ref = s;
            return v;
        }
        default:
            return MakeHandle(name, WrapOfToken(entry->Token_Number), entry->Token_Number, owned);
    }
}

PValue Pov4Evaluator::ClassicEval(const std::string& expr, const Node* at)
{
    std::string name = NewName();
    Flush();
    struct Level { size_t& level; ~Level() { level = 0; } } level{mParser.mOptionalSemicolonLevel};
    level.level = mParser.Cond_Stack.size();
    Snippet("#declare " + name + " = " + expr + "\n", {(at != nullptr) ? POV_LONG(at->line) : 1}, (at != nullptr) ? at->file : 0);
    SymbolTable* global = mParser.mSymbolStack.GetGlobalTable();
    SYM_ENTRY* entry = global->Find_Symbol(name.c_str());
    if (entry == nullptr)
        Fail(at, "Classic SDL produced no value for '%s'.", expr.c_str());
    PValue v = FromClassic(entry, name, true);
    if (v.type != PValue::Handle)
        global->Remove_Symbol(name.c_str(), false, nullptr, 0);
    return v;
}

int Pov4Evaluator::ArrayShape(const PValues& a, int* sizes)
{
    sizes[0] = int(a.size());
    if (a.empty() || (a.front().type != PValue::Arr))
        return 1;
    DepthGuard guard(*this, nullptr);
    int inner[kMaxDims], other[kMaxDims];
    int dims = ArrayShape(a.front().As<PValues>(), inner);
    if (dims >= kMaxDims)
        return 1;
    for (const PValue& e : a)
        if ((e.type != PValue::Arr) || (ArrayShape(e.As<PValues>(), other) != dims) || !std::equal(inner, inner + dims, other))
            return 1;
    std::copy(inner, inner + dims, sizes + 1);
    return dims + 1;
}

bool Pov4Evaluator::ArrayText(std::string& out, const PValues& a, const std::function<bool(std::string&, const PValue&)>& element)
{
    int sizes[kMaxDims];
    int dims = ArrayShape(a, sizes);
    std::vector<const PValue*> leaves;
    std::function<void(const PValues&, int)> collect = [&](const PValues& e, int d) {
        for (const PValue& v : e)
        {
            if (d + 1 < dims)
                collect(v.As<PValues>(), d + 1);
            else
                leaves.push_back(&v);
        }
    };
    collect(a, 0);
    const PValue& first = *leaves.front();
    bool mixed = false;
    for (const PValue* e : leaves)
        mixed = mixed || (e->type != first.type) || (e->size != first.size) || (e->type == PValue::Arr) ||
                ((e->type == PValue::Handle) && (e->As<HandleData>().token != first.As<HandleData>().token));
    out += mixed ? "array mixed" : "array";
    for (int d = 0; d < dims; ++d)
        out += "[" + std::to_string(sizes[d]) + "]";
    std::function<bool(const PValues&, int)> text = [&](const PValues& e, int d) {
        out += '{';
        for (size_t i = 0; i < e.size(); ++i)
        {
            out += (i > 0) ? ", " : " ";
            if (!((d + 1 < dims) ? text(e[i].As<PValues>(), d + 1) : element(out, e[i])))
                return false;
        }
        out += " }";
        return true;
    };
    out += ' ';
    return text(a, 0);
}

void Pov4Evaluator::CollectRefs(const PValue& v)
{
    if (v.type == PValue::Handle)
    {
        mPrinted.push_back(v.ref);
        return;
    }
    if ((v.type != PValue::Arr) && (v.type != PValue::Dict) && (v.type != PValue::Frag))
        return;
    DepthGuard guard(*this, nullptr);
    if (v.type == PValue::Arr)
        for (const PValue& e : v.As<PValues>())
            CollectRefs(e);
    else if (v.type == PValue::Dict)
        for (const auto& kv : v.As<DictData>().items)
            CollectRefs(kv.second);
    else if (v.type == PValue::Frag)
        for (const Item& item : v.As<Items>())
        {
            mPrinted.insert(mPrinted.end(), item.refs.begin(), item.refs.end());
            CollectRefs(item.value);
        }
}

PValue Pov4Evaluator::DeclareBlock(const std::string& keyword, const std::string& text, const Node* at,
                                   const std::vector<std::shared_ptr<void>>& refs)
{
    if (IN_LIST(kUndeclarable, keyword))
    {
        PValue v;
        v.type = PValue::Frag;
        Item item(Item::Block, text, keyword, at);
        item.refs = refs;
        v.ref = NewContainer<Items>(Items{item});
        return v;
    }
    std::string name = NewName();
    AppendPending("#declare " + name + " = " + text + ";", at);
    ClassicRan(refs);
    TokenId token = (keyword == "function") ? FUNCT_ID_TOKEN : (keyword == "spline") ? SPLINE_ID_TOKEN : OBJECT_ID_TOKEN;
    return MakeHandle(name, WrapOfKeyword(keyword), token, true);
}

bool Pov4Evaluator::ClassicLookup(const std::string& name, PValue& out)
{
    if (mPendingRaw)
        Flush();
    SYM_ENTRY* entry = mParser.mSymbolStack.Find_Symbol(name.c_str());
    if (entry == nullptr)
        return false;
    out = FromClassic(entry, name, false);
    return true;
}

std::string Pov4Evaluator::ExportText(const PValue& v, const Node* at)
{
    std::string text;
    if (!ExportInto(text, v, at))
        text.clear();
    return text;
}

bool Pov4Evaluator::ExportInto(std::string& out, const PValue& v, const Node* at)
{
    switch (v.type)
    {
        case PValue::Num:
        case PValue::Vec:
            for (int i = 0; i < v.size; ++i)
                if (std::isnan(v.v[i]))
                    return false;
            break;
        case PValue::Str:
        case PValue::Handle:
            break;
        case PValue::Arr:
        {
            DepthGuard guard(*this, at);
            const PValues& a = v.As<PValues>();
            if (a.empty())
                return false;
            return ArrayText(out, a, [&](std::string& o, const PValue& e) {
                return (e.type != PValue::Arr) && (e.type != PValue::Frag) && ExportInto(o, e, at);
            });
        }
        case PValue::Dict:
        {
            DepthGuard guard(*this, at);
            out += "dictionary {";
            bool first = true;
            for (const auto& kv : v.As<DictData>().items)
            {
                out += first ? " [" : ", [";
                first = false;
                out += EscapeString(UTF8toUCS2String(kv.first)) + "]: ";
                if ((kv.second.type == PValue::Frag) || !ExportInto(out, kv.second, at))
                    return false;
            }
            out += " }";
            return true;
        }
        case PValue::Frag:
        {
            const Items& items = v.As<Items>();
            for (const Item& item : items)
            {
                if ((item.kind != Item::Block) || IN_LIST(kUndeclarable, item.name) ||
                    ((items.size() > 1) && (Normalised(item.name) != "texture")))
                    return false;
                out += (&item == &items.front()) ? "" : " ";
                out += item.text;
            }
            return !items.empty();
        }
        default:
            return false;
    }
    Print(out, v, at, false);
    return true;
}

void Pov4Evaluator::MarkDirty(const Node* target)
{
    while ((target->kind == NK::Index) || (target->kind == NK::Member))
        target = target->a;
    if ((target->kind == NK::Ident) && (mFileScope->vars.count(target->sym) != 0))
        mDirty.insert(target->sym);
}

void Pov4Evaluator::ExportBindings(const Node* at, Output* out, SyncData* sync)
{
    std::vector<int> syms(mDirty.begin(), mDirty.end());
    std::sort(syms.begin(), syms.end());
    mDirty.clear();
    for (int sym : syms)
    {
        auto bound = mFileScope->vars.find(sym);
        if (bound == mFileScope->vars.end())
            continue;
        const std::string& name = mSymbolNames[sym];
        std::string rhs = ExportText(bound->second, at);
        auto previous = mExported.find(sym);
        std::string text;
        if (!rhs.empty())
            text = "#declare " + name + " = " + rhs + ";";
        else if (previous != mExported.end())
            text = "#ifdef (" + name + ") #undef " + name + " #end";
        if (text.empty() || ((sync == nullptr) && (previous != mExported.end()) && (previous->second == rhs)))
            continue;
        if (sync != nullptr)
        {
            size_t mark = mPrinted.size();
            CollectRefs(bound->second);
            out->Put(TextItem(Item::Raw, text, std::string(), at, mark), false);
            sync->syms.push_back(sym);
            ++mInFlight[sym];
        }
        else
            AppendPending(text, at);
        mExported[sym] = rhs;
    }
}

void Pov4Evaluator::ImportBindings()
{
    SymbolTable* global = mParser.mSymbolStack.GetGlobalTable();
    for (auto& exported : mExported)
    {
        auto bound = mFileScope->vars.find(exported.first);
        if ((bound == mFileScope->vars.end()) || (mInFlight.count(exported.first) != 0) ||
            ((bound->second.type != PValue::Num) && (bound->second.type != PValue::Vec) && (bound->second.type != PValue::Str)))
            continue;
        const std::string& name = mSymbolNames[exported.first];
        SYM_ENTRY* entry = global->Find_Symbol(name.c_str());
        if (entry == nullptr)
            continue;
        PValue now = FromClassic(entry, name, false);
        if ((now.type != PValue::Num) && (now.type != PValue::Vec) && (now.type != PValue::Str))
            continue;
        std::string text = ExportText(now, nullptr);
        if (text != exported.second)
        {
            bound->second = now;
            exported.second = text;
        }
    }
}

void Pov4Evaluator::Settle(SyncData* sync)
{
    if (mClosing || (mSyncs.erase(sync) == 0))
        return;
    for (int sym : sync->syms)
    {
        auto i = mInFlight.find(sym);
        if ((i != mInFlight.end()) && (--i->second == 0))
            mInFlight.erase(i);
    }
}

void Pov4Evaluator::ClassicRan(const std::vector<std::shared_ptr<void>>& refs)
{
    if (mSyncs.empty())
        return;
    bool ran = false;
    for (const std::shared_ptr<void>& r : refs)
        if (mSyncs.count(r.get()) != 0)
        {
            Settle(static_cast<SyncData*>(r.get()));
            ran = true;
        }
    if (ran)
    {
        Flush();
        ImportBindings();
    }
}

//------------------------------------------------------------------------------
// Outputs

void Pov4Evaluator::TextOutput::Append(const std::string& text, const Node* at)
{
    if ((at != nullptr) && (at->file == file) && (at->line > line))
    {
        buf.append(at->line - line, '\n');
        line = at->line;
    }
    else
        buf += ' ';
    buf += text;
    line += unsigned(std::count(text.begin(), text.end(), '\n'));
}

void Pov4Evaluator::TextOutput::Put(const Item& item, bool spliced)
{
    switch (item.kind)
    {
        case Item::Val:
            PutValue(item.value, item.at, spliced);
            return;
        case Item::Comma:
            buf += ',';
            lastValue = false;
            lastKeyword.clear();
            return;
        case Item::Keyword:
            ev.mPrinted.insert(ev.mPrinted.end(), item.refs.begin(), item.refs.end());
            Append(item.text, item.at);
            lastKeyword = item.text;
            lastValue = false;
            return;
        default:
            ev.mPrinted.insert(ev.mPrinted.end(), item.refs.begin(), item.refs.end());
            Append(item.text, item.at);
            lastKeyword.clear();
            lastValue = false;
            return;
    }
}

void Pov4Evaluator::TextOutput::PutValue(const PValue& v, const Node* at, bool spliced)
{
    switch (v.type)
    {
        case PValue::Null:
            return;
        case PValue::Frag:
            for (const Item& item : v.As<Items>())
                Put(item, true);
            return;
        case PValue::Handle:
        {
            const HandleData& h = v.As<HandleData>();
            ev.mPrinted.push_back(v.ref);
            if (h.wrap.empty() && lastValue && spliced)
                buf += ',';
            bool copied = (h.wrap == "object") && !spliced && (buf.back() == '{') && IN_LIST(kObjectKeywords, context) &&
                          !IN_LIST(kChildKeywords, context);
            if (h.wrap.empty() || context.empty() || (context == h.wrap) || (lastKeyword == h.wrap) || copied)
                Append(h.name, at);
            else
                Append(h.wrap + " { " + h.name + " }", at);
            lastValue = h.wrap.empty();
            lastKeyword.clear();
            return;
        }
        case PValue::Num:
        case PValue::Vec:
        case PValue::Str:
        {
            std::string text;
            ev.Print(text, v, at, true);
            if (lastValue)
            {
                if (spliced)
                    buf += ',';
                else if ((text[0] == '-') || (text[0] == '+'))
                    text = "(" + text + ")";
            }
            Append(text, at);
            lastValue = true;
            lastKeyword.clear();
            return;
        }
        case PValue::Arr:
            ev.Fail(at, "An array in a block body must be spliced with '...'.");
        default:
            ev.Fail(at, "A %s cannot be used in a block body.", ev.TypeName(v).c_str());
    }
}

void Pov4Evaluator::SceneOutput::Put(const Item& item, bool spliced)
{
    switch (item.kind)
    {
        case Item::Val:
        {
            const PValue& v = item.value;
            if (v.type == PValue::Null)
                return;
            if (v.type == PValue::Frag)
            {
                for (const Item& i : v.As<Items>())
                    Put(i, true);
                return;
            }
            if (v.type == PValue::Handle)
            {
                const HandleData& h = v.As<HandleData>();
                if ((h.wrap == "object") || IN_LIST(kSceneKeywords, h.wrap))
                {
                    ev.AppendPending(h.wrap + " { " + h.name + " }", item.at);
                    return;
                }
            }
            ev.Fail(item.at, "A %s cannot appear at the top level of a scene.", ev.TypeName(v).c_str());
        }
        case Item::Block:
            if (!IN_LIST(kObjectKeywords, item.name) && !IN_LIST(kSceneKeywords, item.name))
                ev.Fail(item.at, "A '%s' block cannot appear at the top level of a scene.", item.name.c_str());
            ev.AppendPending(item.text, item.at);
            ev.ClassicRan(item.refs);
            return;
        case Item::Raw:
            ev.AppendPending(item.text, item.at);
            ev.mPendingRaw = true;
            return;
        case Item::Comma:
            return;
        default:
            ev.Fail(item.at, "'%s' cannot appear at the top level of a scene.", item.text.c_str());
    }
}

void Pov4Evaluator::ListOutput::Put(const Item& item, bool spliced)
{
    if ((item.kind == Item::Val) && (item.value.type == PValue::Frag))
    {
        for (const Item& i : item.value.As<Items>())
            items.push_back(i);
        return;
    }
    items.push_back(item);
}

void Pov4Evaluator::ArrayOutput::Put(const Item& item, bool spliced)
{
    if (item.kind == Item::Comma)
        return;
    if ((item.kind == Item::Val) && (item.value.type == PValue::Frag))
    {
        for (const Item& i : item.value.As<Items>())
            Put(i, true);
        return;
    }
    if (values.size() >= kMaxArray)
        ev.Fail(item.at, "An array cannot have more than %u elements.", unsigned(kMaxArray));
    values.push_back(ev.Store(ev.ToValue(item), item.at));
}

//------------------------------------------------------------------------------
// PValues

PValue Pov4Evaluator::MakeString(const UCS2String& s)
{
    PValue v;
    v.type = PValue::Str;
    auto d = std::make_shared<StrData>();
    d->s = s;
    v.ref = d;
    return v;
}

PValue Pov4Evaluator::MakeArray(PValues&& values)
{
    PValue v;
    v.type = PValue::Arr;
    v.ref = NewContainer<PValues>(std::move(values));
    return v;
}

PValue Pov4Evaluator::ToValue(const Item& item)
{
    switch (item.kind)
    {
        case Item::Val:     return item.value;
        case Item::Block:   return DeclareBlock(item.name, item.text, item.at, item.refs);
        case Item::Raw:     return ClassicEval(item.text, item.at);
        default:            Fail(item.at, "'%s' is not a value.", item.text.c_str());
    }
}

PValue Pov4Evaluator::AsValue(const PValue& v, const Node* at)
{
    if (v.type != PValue::Frag)
        return v;
    const Items& items = v.As<Items>();
    if (items.empty())
        return PValue();
    if (items.size() == 1)
    {
        if ((items[0].kind == Item::Block) && IN_LIST(kUndeclarable, items[0].name))
            return v;
        return ToValue(items[0]);
    }
    return v;
}

PValue Pov4Evaluator::Store(const PValue& v, const Node* at)
{
    PValue r = AsValue(v, at);
    if ((r.type == PValue::Vec) && (r.size == 5))
        for (int i = 0; i < 5; ++i)
            r.v[i] = double(ColourChannel(r.v[i]));
    return r;
}

void Pov4Evaluator::Print(std::string& out, const PValue& v, const Node* at, bool colourKeyword)
{
    switch (v.type)
    {
        case PValue::Num:
            if (std::isnan(v.v[0]))
                Fail(at, "Not a number.");
            AppendNumber(out, v.v[0]);
            return;
        case PValue::Vec:
            if (colourKeyword && v.colour)
                out += "rgbft ";
            out += '<';
            for (int i = 0; i < v.size; ++i)
            {
                if (i > 0)
                    out += ", ";
                if (std::isnan(v.v[i]))
                    Fail(at, "Not a number.");
                AppendNumber(out, v.v[i]);
            }
            out += '>';
            return;
        case PValue::Str:
        {
            const StrData& s = v.As<StrData>();
            out += s.raw.empty() ? EscapeString(s.s) : s.raw;
            return;
        }
        case PValue::Handle:
            out += v.As<HandleData>().name;
            return;
        case PValue::Frag:
        {
            PValue single = AsValue(v, at);
            if (single.type == PValue::Frag)
                Fail(at, "A %s cannot be passed to classic SDL.", TypeName(v).c_str());
            Print(out, single, at, colourKeyword);
            return;
        }
        case PValue::Arr:
        {
            DepthGuard guard(*this, at);
            const PValues& a = v.As<PValues>();
            if (a.empty())
                Fail(at, "An empty array cannot be passed to classic SDL.");
            ArrayText(out, a, [&](std::string& o, const PValue& e) { Print(o, e, at, false); return true; });
            return;
        }
        case PValue::Dict:
        {
            DepthGuard guard(*this, at);
            out += "dictionary {";
            bool first = true;
            for (const auto& kv : v.As<DictData>().items)
            {
                out += first ? " [" : ", [";
                first = false;
                out += EscapeString(UTF8toUCS2String(kv.first)) + "]: ";
                Print(out, kv.second, at, false);
            }
            out += " }";
            return;
        }
        default:
            Fail(at, "A %s cannot be passed to classic SDL.", TypeName(v).c_str());
    }
}

double Pov4Evaluator::Float(const PValue& v, const Node* at, const char* what)
{
    if (v.type == PValue::Num)
        return v.v[0];
    if (v.type == PValue::Frag)
        return Float(AsValue(v, at), at, what);
    Fail(at, "%s must be a float, not a %s.", what, TypeName(v).c_str());
}

Vector3d Pov4Evaluator::Vec3(const PValue& v0, const Node* at)
{
    PValue v = AsValue(v0, at);
    if (v.type == PValue::Num)
        return Vector3d(v.v[0], v.v[0], v.v[0]);
    if ((v.type == PValue::Vec) && (v.size <= 3))
        return Vector3d(v.v[0], v.v[1], (v.size == 3) ? v.v[2] : 0.0);
    Fail(at, "Vector expected but %s found.", TypeName(v).c_str());
}

const UCS2String& Pov4Evaluator::Str(const PValue& v, const Node* at)
{
    if (v.type != PValue::Str)
        Fail(at, "String expected but %s found.", TypeName(v).c_str());
    return v.As<StrData>().s;
}

bool Pov4Evaluator::Truth(const PValue& v, const Node* at)
{
    return FTrue(Float(v, at, "A condition"));
}

//------------------------------------------------------------------------------
// Statements and emission

Pov4Evaluator::Flow Pov4Evaluator::ExecItems(const std::vector<const Node*>& items, Output& out)
{
    for (const Node* n : items)
    {
        Flow f = Exec(n, out);
        if (f != Flow::Normal)
            return f;
    }
    return Flow::Normal;
}

PValue* Pov4Evaluator::FindBinding(int sym)
{
    for (Scope* s = mScope.get(); s != nullptr; s = s->parent.get())
    {
        auto i = s->vars.find(sym);
        if (i != s->vars.end())
            return &i->second;
    }
    return nullptr;
}

PValue* Pov4Evaluator::FindBinding(const Node* n)
{
    if ((n->cacheScope == mScope.get()) && (n->cacheEpoch == mEpoch))
        return static_cast<PValue*>(n->cacheSlot);
    PValue* slot = FindBinding(n->sym);
    if (slot != nullptr)
    {
        n->cacheScope = mScope.get();
        n->cacheSlot = slot;
        n->cacheEpoch = mEpoch;
    }
    return slot;
}

void Pov4Evaluator::Bind(int sym, const PValue& v, bool global)
{
    if (global || (mScope == mFileScope))
        mDirty.insert(sym);
    std::unordered_map<int, PValue>& vars = (global ? mFileScope : mScope)->vars;
    auto slot = vars.find(sym);
    if (slot != vars.end())
        slot->second = v;
    else
    {
        vars.emplace(sym, v);
        ++mEpoch;
    }
}

PValue& Pov4Evaluator::LRef(const Node* n)
{
    if (n->kind == NK::Ident)
    {
        PValue* v = FindBinding(n);
        if (v == nullptr)
            Fail(n, "'%s' has no binding to assign to; declare it with 'let'.", n->text.c_str());
        return *v;
    }
    if (n->kind == NK::Index)
    {
        PValue index = Eval(n->b);
        PValue& base = LRef(n->a);
        if (base.type == PValue::Dict)
        {
            if (base.ref.use_count() > 1)
                base.ref = NewContainer<DictData>(base.As<DictData>());
            std::string key = UCS2toUTF8(Str(index, n->b));
            DictData& d = base.As<DictData>();
            if (d.Find(key) == nullptr)
                d.Set(key, PValue());
            return *d.Find(key);
        }
        if (base.type != PValue::Arr)
            Fail(n, "Cannot index a %s.", TypeName(base).c_str());
        if (base.ref.use_count() > 1)
            base.ref = NewContainer<PValues>(base.As<PValues>());
        PValues& a = base.As<PValues>();
        double i = Float(index, n->b, "An array index");
        if ((i < 0) || (size_t(i) >= a.size()))
            Fail(n->b, "Index %g is outside the array (size %u).", i, unsigned(a.size()));
        return a[size_t(i)];
    }
    if (n->kind == NK::Member)
        return DictMember(LRef(n->a), n);
    Fail(n, "Cannot assign to this expression.");
}

PValue& Pov4Evaluator::DictMember(PValue& base, const Node* n)
{
    if (base.type != PValue::Dict)
        Fail(n, "Cannot assign the member '%s' of a %s.", n->text.c_str(), TypeName(base).c_str());
    if (base.ref.use_count() > 1)
        base.ref = NewContainer<DictData>(base.As<DictData>());
    DictData& d = base.As<DictData>();
    if (d.Find(n->text) == nullptr)
        d.Set(n->text, PValue());
    return *d.Find(n->text);
}

void Pov4Evaluator::Assign(const Node* target, const PValue& v)
{
    if (target->kind != NK::Member)
        LRef(target) = v;
    else
    {
        PValue& base = LRef(target->a);
        if (base.type != PValue::Vec)
            DictMember(base, target) = v;
        else
        {
            int i = ComponentIndex(target->text);
            if ((i < 0) || (i >= base.size))
                Fail(target, "A %s has no component '%s'.", TypeName(base).c_str(), target->text.c_str());
            double c = Float(v, target, "A component");
            base.v[i] = (base.size == 5) ? double(ColourChannel(c)) : c;
        }
    }
    MarkDirty(target);
}

Pov4Evaluator::Flow Pov4Evaluator::Exec(const Node* n, Output& out)
{
    DepthGuard guard(*this, n);
    if ((++mSteps & 4095) == 0)
        mParser.mProgressReporter.ReportProgress(mParser.mTokenCount + POV_LONG(mSteps));
    switch (n->kind)
    {
        case NK::Let:
        case NK::Global:
        case NK::Assign:
        {
            PValue v;
            if (n->kids.size() > 1)
            {
                v.type = PValue::Frag;
                auto items = NewContainer<Items>();
                for (const Node* k : n->kids)
                {
                    size_t mark = mPrinted.size();
                    std::string text = LowerBlock(k);
                    items->push_back(TextItem(Item::Block, text, k->text, k, mark));
                }
                v.ref = items;
            }
            else if (n->a != nullptr)
                v = Store(Eval(n->a), n->a);
            if (n->kind == NK::Assign)
                Assign(n->c, v);
            else
                Bind(n->sym, v, n->kind == NK::Global);
            return Flow::Normal;
        }
        case NK::FnDef:
            Bind(n->sym, Eval(n), false);
            return Flow::Normal;
        case NK::If:
            for (const Node* branch = n; branch != nullptr; )
            {
                if (Truth(Eval(branch->a), branch->a))
                    return ExecItems(branch->b->kids, out);
                if ((branch->c != nullptr) && (branch->c->kind == NK::Body))
                    return ExecItems(branch->c->kids, out);
                branch = branch->c;
            }
            return Flow::Normal;
        case NK::While:
            while (Truth(Eval(n->a), n->a))
            {
                Flow f = ExecItems(n->b->kids, out);
                if (f == Flow::Break)
                    break;
                if (f == Flow::Return)
                    return f;
            }
            return Flow::Normal;
        case NK::For:
        {
            double current = Float(Eval(n->a), n->a, "A loop start");
            double end = Float(Eval(n->b), n->b, "A loop end");
            double step = (n->c != nullptr) ? Float(Eval(n->c), n->c, "A loop step") : 1.0;
            if (std::fabs(step) < EPSILON)
                Fail(n->c, "A loop step must be non-zero.");
            Bind(n->sym, PValue::Number(current), false);
            if (!(((step > 0) && (current < end + EPSILON)) || ((step < 0) && (current > end - EPSILON))))
                return Flow::Normal;
            while (true)
            {
                Flow f = ExecItems(n->d->kids, out);
                if (f == Flow::Break)
                    break;
                if (f == Flow::Return)
                    return f;
                PValue* var = FindBinding(n);
                if ((var == nullptr) || (var->type != PValue::Num))
                    Fail(n, "The loop variable must remain a float during the loop.");
                current = var->v[0] + step;
                Bind(n->sym, PValue::Number(current), false);
                if (((step > 0) && (current > end + EPSILON)) || ((step < 0) && (current < end - EPSILON)))
                    break;
            }
            return Flow::Normal;
        }
        case NK::ForIn:
        {
            PValue it = AsValue(Eval(n->a), n->a);
            PValues elements;
            if (it.type == PValue::Dict)
                for (const auto& kv : it.As<DictData>().items)
                    elements.push_back(MakeString(UTF8toUCS2String(kv.first)));
            else if (it.type == PValue::Frag)
                for (const Item& item : it.As<Items>())
                    elements.push_back(ToValue(item));
            else if (it.type != PValue::Arr)
                Fail(n->a, "Cannot iterate over a %s.", TypeName(it).c_str());
            for (const PValue& e : (it.type == PValue::Arr) ? it.As<PValues>() : elements)
            {
                Bind(n->sym, e, false);
                Flow f = ExecItems(n->d->kids, out);
                if (f == Flow::Break)
                    break;
                if (f == Flow::Return)
                    return f;
            }
            return Flow::Normal;
        }
        case NK::Break:
            return Flow::Break;
        case NK::Continue:
            return Flow::Continue;
        case NK::Return:
            mReturn = (n->a != nullptr) ? Eval(n->a) : PValue();
            mReturnBare = (n->a == nullptr);
            return Flow::Return;
        case NK::Include:
        {
            UCS2String file = Str(AsValue(Eval(n->a), n->a), n->a);
            if (IsPov4File(file))
            {
                if (mIncludeDepth >= kMaxIncludeDepth)
                    Fail(n, "Too many nested .inc4 includes (more than %d).", kMaxIncludeDepth);
                struct Count { int& depth; ~Count() { --depth; } } count{++mIncludeDepth};
                Program& p = Load(file, POV_File_Text_INC, n);
                Flow f = ExecItems(p.root->kids, out);
                if (f != Flow::Normal)
                    Fail(n, "'break', 'continue' or 'return' outside a loop or function in '%s'.", UCS2toSysString(file).c_str());
            }
            else if (out.IsBlock())
            {
                auto sync = std::make_shared<SyncData>(this);
                mSyncs.insert(sync.get());
                ExportBindings(n, &out, sync.get());
                mPrinted.push_back(sync);
                out.Put(TextItem(Item::Raw, "#include " + EscapeString(file), std::string(), n, mPrinted.size() - 1), false);
            }
            else
            {
                ExportBindings(n, nullptr, nullptr);
                Flush();
                Snippet("#include " + EscapeString(file) + "\n", {POV_LONG(n->line)}, n->file);
                ImportBindings();
            }
            return Flow::Normal;
        }
        default:
            Emit(n, out);
            return Flow::Normal;
    }
}

void Pov4Evaluator::Splice(const PValue& v0, Output& out, const Node* at)
{
    PValue v = (v0.type == PValue::Frag) ? v0 : AsValue(v0, at);
    if (v.type == PValue::Arr)
        for (const PValue& e : v.As<PValues>())
            out.Put(Item(e, at), true);
    else if (v.type == PValue::Frag)
        for (const Item& item : v.As<Items>())
            out.Put(item, true);
    else if (v.type != PValue::Null)
        Fail(at, "Cannot splice a %s.", TypeName(v).c_str());
}

void Pov4Evaluator::Emit(const Node* n, Output& out)
{
    switch (n->kind)
    {
        case NK::Keyword:
            out.Put(Item(Item::Keyword, n->text, n->text, n), false);
            return;
        case NK::Ident:
            if (out.IsBlock() && (static_cast<TextOutput&>(out).lastKeyword == "mix"))
            {
                out.Put(Item(Item::Keyword, n->text, n->text, n), false);
                return;
            }
            break;
        case NK::Comma:
            out.Put(Item(Item::Comma, ",", std::string(), n), false);
            return;
        case NK::Spread:
            Splice(Eval(n->a), out, n);
            return;
        case NK::Block:
        case NK::FunctionBlock:
        {
            size_t mark = mPrinted.size();
            std::string text = (n->kind == NK::Block) ? LowerBlock(n) : LowerFunction(n);
            out.Put(TextItem(Item::Block, text, (n->kind == NK::Block) ? n->text : "function", n, mark), false);
            return;
        }
        case NK::Array:
            if (out.IsBlock())
            {
                size_t mark = mPrinted.size();
                TextOutput group(*this, std::string(), n);
                group.buf = "[";
                if (ExecItems(n->kids, group) != Flow::Normal)
                    Fail(n, "'break', 'continue' or 'return' inside a bracket group.");
                out.Put(TextItem(Item::Bracket, group.buf + " ]", std::string(), n, mark), false);
                return;
            }
            break;
        case NK::Call:
            CallEmit(n, out);
            return;
        case NK::Vector:
            if (out.IsBlock() && (n->kids.size() > 5))
            {
                std::string text = "<";
                for (size_t i = 0; i < n->kids.size(); ++i)
                {
                    text += (i > 0) ? ", " : "";
                    Print(text, PValue::Number(Float(Eval(n->kids[i]), n->kids[i], "A vector component")), n->kids[i], false);
                }
                out.Put(Item(Item::Raw, text + ">", std::string(), n), false);
                return;
            }
            break;
        case NK::Colour:
            if (out.IsBlock())
            {
                size_t mark = mPrinted.size();
                std::string text = ColourText(n);
                out.Put(TextItem(Item::Keyword, text, std::string(), n, mark), false);
                return;
            }
            break;
        case NK::Builtin:
            if (out.IsBlock() && (n->op >= B_abs))
            {
                out.Put(Item(Item::Keyword, n->text, n->text, n), false);
                return;
            }
            break;
        default:
            break;
    }
    out.Put(Item(Eval(n), n), false);
}

std::string Pov4Evaluator::LowerBlock(const Node* n)
{
    TextOutput block(*this, (n->text == "pigment_pattern") ? "pigment" : Normalised(n->text), n);
    block.buf = n->text + " {";
    if (ExecItems(n->kids, block) != Flow::Normal)
        Fail(n, "'break', 'continue' or 'return' inside a '%s' block.", n->text.c_str());
    return block.buf + " }";
}

//------------------------------------------------------------------------------
// Render-time functions

std::string Pov4Evaluator::LowerFunction(const Node* n)
{
    std::unordered_set<int> params;
    std::string out = "function";
    if (n->a != nullptr)
    {
        out += '(';
        for (size_t i = 0; i < n->a->kids.size(); ++i)
        {
            out += (i > 0) ? ", " : "";
            out += n->a->kids[i]->text;
            params.insert(n->a->kids[i]->sym);
        }
        out += ')';
    }
    else
        for (const char* p : {"x", "y", "z"})
            params.insert(Intern(p));
    if (n->b != nullptr)
    {
        out += ' ';
        Print(out, AsValue(Eval(n->b), n->b), n->b, false);
        out += ", ";
        Print(out, AsValue(Eval(n->c), n->c), n->c, false);
    }
    out += " {";
    for (size_t i = 0; i < n->kids.size(); ++i)
    {
        const Node* item = n->kids[i];
        out += ' ';
        if (item->kind == NK::Block)
            out += LowerBlock(item);
        else if (item->kind == NK::Keyword)
            out += item->text;
        else if (item->kind == NK::Comma)
            out += ',';
        else if ((i > 0) && (n->kids[i - 1]->kind == NK::Keyword))
        {
            out += '(';
            PrintFunctionExpr(out, item, params);
            out += ')';
        }
        else
            PrintFunctionExpr(out, item, params);
    }
    return out + " }";
}

void Pov4Evaluator::PrintFunctionExpr(std::string& out, const Node* n, const std::unordered_set<int>& params)
{
    DepthGuard guard(*this, n);
    static const char* const ops[] = {"|", "&", "=", "!=", "<", "<=", ">", ">=", "+", "-", "*", "/"};
    switch (n->kind)
    {
        case NK::Number:
            AppendNumber(out, n->num);
            return;
        case NK::String:
            out += n->text;
            return;
        case NK::Builtin:
        case NK::Keyword:
            out += n->text;
            return;
        case NK::True:
            out += '1';
            return;
        case NK::False:
            out += '0';
            return;
        case NK::Ident:
        {
            if (params.count(n->sym) != 0)
            {
                out += n->text;
                return;
            }
            PValue* bound = FindBinding(n->sym);
            if (bound == nullptr)
            {
                out += n->text;
                return;
            }
            PValue v = AsValue(*bound, n);
            if (v.type == PValue::Handle)
                mPrinted.push_back(v.ref);
            if ((v.type == PValue::Num) || (v.type == PValue::Handle))
            {
                if ((v.type == PValue::Num) && (v.v[0] < 0))
                {
                    out += '(';
                    Print(out, v, n, false);
                    out += ')';
                }
                else
                    Print(out, v, n, false);
                return;
            }
            if (v.type == PValue::Fn)
                Fail(n, "'%s' is a 4.0 function, which cannot be called from a render-time function.", n->text.c_str());
            Fail(n, "'%s' is a %s, which a render-time function cannot use.", n->text.c_str(), TypeName(v).c_str());
        }
        case NK::Member:
            if ((n->a->kind == NK::Ident) && (params.count(n->a->sym) == 0) && (FindBinding(n->a->sym) != nullptr))
            {
                PValue v = EvalMember(n);
                out += '(';
                Print(out, v, n, false);
                out += ')';
                return;
            }
            PrintFunctionExpr(out, n->a, params);
            out += '.' + n->text;
            return;
        case NK::Binary:
            out += '(';
            PrintFunctionExpr(out, n->a, params);
            out += std::string(" ") + ops[n->op] + " ";
            PrintFunctionExpr(out, n->b, params);
            out += ')';
            return;
        case NK::Unary:
            if (n->op == OpNot)
            {
                out += "select(-abs(";
                PrintFunctionExpr(out, n->a, params);
                out += "), 0, 1)";
                return;
            }
            out += (n->op == OpNeg) ? "(-" : "(";
            PrintFunctionExpr(out, n->a, params);
            out += ')';
            return;
        case NK::Cond:
            out += "select(-abs(";
            PrintFunctionExpr(out, n->a, params);
            out += "), ";
            PrintFunctionExpr(out, n->b, params);
            out += ", ";
            PrintFunctionExpr(out, n->c, params);
            out += ')';
            return;
        case NK::Call:
            PrintFunctionExpr(out, n->a, params);
            out += '(';
            for (size_t i = 0; i < n->kids.size(); ++i)
            {
                out += (i > 0) ? ", " : "";
                PrintFunctionExpr(out, n->kids[i], params);
            }
            out += ')';
            return;
        case NK::Vector:
            out += '<';
            for (size_t i = 0; i < n->kids.size(); ++i)
            {
                out += (i > 0) ? ", " : "";
                PrintFunctionExpr(out, n->kids[i], params);
            }
            out += '>';
            return;
        case NK::Block:
            out += LowerBlock(n);
            return;
        default:
            Fail(n, "This expression cannot be used in a render-time function.");
    }
}

//------------------------------------------------------------------------------
// Expressions

PValue Pov4Evaluator::Lookup(const Node* n)
{
    if (PValue* v = FindBinding(n))
        return *v;
    PValue v;
    if (ClassicLookup(n->text, v))
        return v;
    auto b = BuiltinNames().find(n->text);
    if (b != BuiltinNames().end())
    {
        v.type = PValue::Builtin;
        v.v[0] = b->second;
        return v;
    }
    Fail(n, "Unknown name '%s'.", n->text.c_str());
}

PValue Pov4Evaluator::Eval(const Node* n)
{
    DepthGuard guard(*this, n);
    switch (n->kind)
    {
        case NK::Number:
            return PValue::Number(n->num);
        case NK::String:
        {
            PValue v = MakeString(n->str);
            v.As<StrData>().raw = n->text;
            return v;
        }
        case NK::True:
            return PValue::Number(1.0);
        case NK::False:
            return PValue::Number(0.0);
        case NK::Null:
            return PValue();
        case NK::Ident:
            return Lookup(n);
        case NK::Builtin:
        {
            if (n->op >= B_abs)
            {
                PValue v;
                v.type = PValue::Builtin;
                v.v[0] = n->op;
                v.ref = std::make_shared<std::string>(n->text);
                return v;
            }
            PValues none;
            return CallBuiltin(BI(n->op), n->text, none, nullptr, n);
        }
        case NK::Vector:
        {
            if (n->kids.size() > 5)
                Fail(n, "A vector of more than 5 components can only be a block item, as in 'matrix <...>'.");
            double e[5];
            for (size_t i = 0; i < n->kids.size(); ++i)
                e[i] = Float(Eval(n->kids[i]), n->kids[i], "A vector component");
            return PValue::Vector(e, int(n->kids.size()));
        }
        case NK::Array:
            return EvalArray(n);
        case NK::Dict:
            return EvalDict(n);
        case NK::Block:
        case NK::FunctionBlock:
        {
            size_t mark = mPrinted.size();
            std::string text = (n->kind == NK::Block) ? LowerBlock(n) : LowerFunction(n);
            Item item = TextItem(Item::Block, text, std::string(), n, mark);
            return DeclareBlock((n->kind == NK::Block) ? n->text : "function", item.text, n, item.refs);
        }
        case NK::Cond:
            return Truth(Eval(n->a), n->a) ? Eval(n->b) : Eval(n->c);
        case NK::Binary:
            return EvalBinary(n);
        case NK::Unary:
        {
            PValue v = AsValue(Eval(n->a), n->a);
            if (!v.IsNumeric())
                Fail(n, "Operator needs a float or vector, not a %s.", TypeName(v).c_str());
            for (int i = 0; i < v.size; ++i)
                v.v[i] = (n->op == OpNeg) ? -v.v[i] : (n->op == OpNot) ? (FTrue(v.v[i]) ? 0.0 : 1.0) : v.v[i];
            v.colour = v.colour && (n->op != OpNot);
            return v;
        }
        case NK::Lambda:
        case NK::FnDef:
        {
            PValue v;
            v.type = PValue::Fn;
            auto c = std::make_shared<Closure>();
            c->params = n->a;
            c->body = n->b;
            c->expressionBody = (n->op != 0);
            c->env = mScope;
            v.ref = c;
            return v;
        }
        case NK::Colour:
            return EvalColour(n);
        case NK::Call:
            return Call(n);
        case NK::Index:
            return EvalIndex(n);
        case NK::Member:
            return EvalMember(n);
        case NK::Keyword:
            Fail(n, "'%s' is not a value.", n->text.c_str());
        default:
            Fail(n, "A statement is not a value.");
    }
}

PValue Pov4Evaluator::EvalArray(const Node* n)
{
    ArrayOutput out(*this);
    if (ExecItems(n->kids, out) != Flow::Normal)
        Fail(n, "'break', 'continue' or 'return' inside an array literal.");
    return MakeArray(std::move(out.values));
}

PValue Pov4Evaluator::EvalDict(const Node* n)
{
    PValue v;
    v.type = PValue::Dict;
    auto d = NewContainer<DictData>();
    for (const Node* k : n->kids)
    {
        if (k->kind == NK::Pair)
        {
            std::string key = (k->b != nullptr) ? UCS2toUTF8(Str(AsValue(Eval(k->b), k->b), k->b)) : k->text;
            d->Set(key, Store(Eval(k->a), k->a));
            continue;
        }
        PValue s = AsValue(Eval(k->a), k->a);
        if (s.type != PValue::Dict)
            Fail(k, "Only a dictionary can be spread into a dictionary, not a %s.", TypeName(s).c_str());
        for (const auto& kv : s.As<DictData>().items)
            d->Set(kv.first, kv.second);
    }
    v.ref = d;
    return v;
}

PValue Pov4Evaluator::EvalBinary(const Node* n)
{
    if ((n->op == OpAnd) || (n->op == OpOr))
    {
        PValue l = AsValue(Eval(n->a), n->a);
        if ((l.type == PValue::Num) && (FTrue(l.v[0]) == (n->op == OpOr)))
            return PValue::Number(n->op == OpOr ? 1.0 : 0.0);
        PValue r = AsValue(Eval(n->b), n->b);
        if (!l.IsNumeric() || !r.IsNumeric())
            Fail(n, "Logical operators need floats or vectors.");
        int size = std::max(l.size, r.size);
        PValue out;
        out.type = (size == 1) ? PValue::Num : PValue::Vec;
        out.size = (unsigned char)size;
        for (int i = 0; i < size; ++i)
        {
            double a = (l.size == 1) ? l.v[0] : (i < l.size) ? l.v[i] : 0.0;
            double b = (r.size == 1) ? r.v[0] : (i < r.size) ? r.v[i] : 0.0;
            out.v[i] = (n->op == OpAnd) ? double(FTrue(a) && FTrue(b)) : double(FTrue(a) || FTrue(b));
        }
        return out;
    }
    PValue l = Eval(n->a);
    if (l.type == PValue::Frag)
        l = AsValue(l, n->a);
    PValue r = Eval(n->b);
    if (r.type == PValue::Frag)
        r = AsValue(r, n->b);
    if ((l.type == PValue::Str) && (r.type == PValue::Str) && (n->op >= OpEq) && (n->op <= OpGe))
    {
        int c = UCS2_strcmp(l.As<StrData>().s.c_str(), r.As<StrData>().s.c_str());
        bool result = (n->op == OpEq) ? (c == 0) : (n->op == OpNe) ? (c != 0) : (n->op == OpLt) ? (c < 0) :
                      (n->op == OpLe) ? (c <= 0) : (n->op == OpGt) ? (c > 0) : (c >= 0);
        return PValue::Number(result ? 1.0 : 0.0);
    }
    if (!l.IsNumeric() || !r.IsNumeric())
        Fail(n, "Cannot apply this operator to a %s and a %s.", TypeName(l).c_str(), TypeName(r).c_str());
    int size = std::max(l.size, r.size);
    PValue out;
    out.type = (size == 1) ? PValue::Num : PValue::Vec;
    out.size = (unsigned char)size;
    out.colour = (size == 5) && (l.colour || r.colour) && (n->op >= OpAdd);
    for (int i = 0; i < size; ++i)
    {
        double a = (l.size == 1) ? l.v[0] : (i < l.size) ? l.v[i] : 0.0;
        double b = (r.size == 1) ? r.v[0] : (i < r.size) ? r.v[i] : 0.0;
        double& o = out.v[i];
        switch (n->op)
        {
            case OpAdd: o = a + b; break;
            case OpSub: o = a - b; break;
            case OpMul: o = a * b; break;
            case OpDiv:
                if (b == 0.0)
                {
                    o = HUGE_VAL;
                    mParser.Warning("Divide by zero.");
                }
                else
                    o = a / b;
                break;
            case OpLt: o = double(a < b); break;
            case OpLe: o = double((a <= b) || !FTrue(a - b)); break;
            case OpEq: o = double(!FTrue(a - b)); break;
            case OpNe: o = double(FTrue(a - b)); break;
            case OpGe: o = double((a >= b) || !FTrue(a - b)); break;
            case OpGt: o = double(a > b); break;
            default: break;
        }
    }
    return out;
}

std::string Pov4Evaluator::ColourText(const Node* n)
{
    std::string text = n->text;
    auto add = [&](const Node* v) {
        if (!text.empty())
            text += ' ';
        if (v->kind == NK::Colour)
            text += ColourText(v);
        else
        {
            PValue value = AsValue(Eval(v), v);
            CollectRefs(value);
            Print(text, value, v, false);
        }
    };
    if (n->a != nullptr)
        add(n->a);
    for (const Node* c : n->kids)
    {
        text += text.empty() ? c->text : " " + c->text;
        if (c->a != nullptr)
            add(c->a);
    }
    return text;
}

PValue Pov4Evaluator::EvalColour(const Node* n)
{
    const std::string& op = n->text;
    EXPRESS e = {0, 0, 0, 0, 0};
    if (!op.empty())
    {
        if (n->a == nullptr)
            Fail(n, "'%s' needs a value here.", op.c_str());
        if (op.compare(0, 4, "srgb") == 0)
            return ClassicEval(ColourText(n), n);
        PValue o = AsValue(Eval(n->a), n->a);
        if (!o.IsNumeric() || (o.size > 5))
            Fail(n->a, "A colour needs a float or vector, not a %s.", TypeName(o).c_str());
        int target = (op == "rgb") ? 3 : ((op == "rgbf") || (op == "rgbt")) ? 4 : 5;
        int terms = o.size;
        for (int i = 0; i < 5; ++i)
            e[i] = (terms == 1) ? o.v[0] : (i < terms) ? o.v[i] : 0.0;
        terms = std::max(terms, target);
        RGBFTColour colour;
        colour.Clear();
        colour.Set(e, terms);
        if (op == "rgbt")
        {
            colour.transm() = colour.filter();
            colour.filter() = 0.0;
        }
        colour.Get(e, 5);
    }
    for (const Node* c : n->kids)
    {
        if (c->a == nullptr)
            Fail(c, "'%s' needs a value here.", c->text.c_str());
        int i = (c->text == "red") ? 0 : (c->text == "green") ? 1 : (c->text == "blue") ? 2 :
                ((c->text == "filter") || (c->text == "alpha")) ? 3 : (c->text == "transmit") ? 4 : -1;
        if (i < 0)
            Fail(c, "'%s' cannot set a colour channel.", c->text.c_str());
        e[i] = double(ColourChannel(Float(Eval(c->a), c->a, "A colour channel")));
    }
    PValue colour = PValue::Vector(e, 5);
    colour.colour = true;
    return colour;
}

PValue Pov4Evaluator::EvalIndex(const Node* n)
{
    PValue base = AsValue(Eval(n->a), n->a);
    PValue index = AsValue(Eval(n->b), n->b);
    if (base.type == PValue::Arr)
    {
        const PValues& a = base.As<PValues>();
        double i = Float(index, n->b, "An array index");
        if ((i < 0) || (size_t(i) >= a.size()))
            Fail(n->b, "Index %g is outside the array (size %u).", i, unsigned(a.size()));
        return a[size_t(i)];
    }
    if (base.type == PValue::Dict)
    {
        PValue* v = base.As<DictData>().Find(UCS2toUTF8(Str(index, n->b)));
        if (v == nullptr)
            Fail(n, "The dictionary has no key '%s'.", UCS2toUTF8(Str(index, n->b)).c_str());
        return *v;
    }
    if (base.type == PValue::Frag)
    {
        const Items& items = base.As<Items>();
        double i = Float(index, n->b, "A fragment index");
        if ((i < 0) || (size_t(i) >= items.size()))
            Fail(n->b, "Index %g is outside the fragment (%u items).", i, unsigned(items.size()));
        return ToValue(items[size_t(i)]);
    }
    if (base.type == PValue::Handle)
    {
        std::string text = base.As<HandleData>().name + "[";
        Print(text, index, n->b, false);
        return ClassicEval(text + "]", n);
    }
    Fail(n, "Cannot index a %s.", TypeName(base).c_str());
}

PValue Pov4Evaluator::EvalMember(const Node* n)
{
    PValue base = AsValue(Eval(n->a), n->a);
    const std::string& m = n->text;
    if (base.type == PValue::Dict)
    {
        PValue* v = base.As<DictData>().Find(m);
        if (v == nullptr)
            Fail(n, "The dictionary has no key '%s'.", m.c_str());
        return *v;
    }
    if (base.type == PValue::Handle)
        return ClassicEval(base.As<HandleData>().name + "." + m, n);
    if (base.IsNumeric())
    {
        if ((m == "gray") || (m == "grey"))
        {
            EXPRESS e = {0, 0, 0, 0, 0};
            for (int i = 0; i < base.size; ++i)
                e[i] = base.v[i];
            if (base.size < 3)
                Fail(n, "Bad operands for period operator.");
            return PValue::Number(PreciseRGBFTColour(e).Greyscale());
        }
        int i = ComponentIndex(m);
        if (i < 0)
            Fail(n, "Unknown component '%s'.", m.c_str());
        if (i >= base.size)
            Fail(n, "Bad operands for period operator.");
        return PValue::Number(base.v[i]);
    }
    Fail(n, "A %s has no member '%s'.", TypeName(base).c_str(), m.c_str());
}

//------------------------------------------------------------------------------
// Calls

void Pov4Evaluator::EvalArgs(const Node* n, PValues& args)
{
    for (const Node* a : n->kids)
    {
        if (a->kind != NK::Spread)
        {
            args.push_back(Eval(a));
            continue;
        }
        PValue s = AsValue(Eval(a->a), a->a);
        if (s.type != PValue::Arr)
            Fail(a, "Only an array can be spread into arguments, not a %s.", TypeName(s).c_str());
        for (const PValue& e : s.As<PValues>())
            args.push_back(e);
    }
}

std::string Pov4Evaluator::ClassicCallText(const std::string& name, const PValues& args, const Node* at)
{
    std::string text = name + "(";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            text += ", ";
        Print(text, args[i], at, false);
    }
    return text + ")";
}

PValue Pov4Evaluator::Call(const Node* n)
{
    if ((n->a->kind == NK::Builtin) && (n->a->op == B_defined))
    {
        if ((n->kids.size() != 1) || (n->kids[0]->kind != NK::Ident))
            Fail(n, "'defined' takes one name.");
        PValue ignored;
        const PValue* bound = FindBinding(n->kids[0]->sym);
        if (bound != nullptr)
            return PValue::Number((bound->type != PValue::Null) ? 1.0 : 0.0);
        return PValue::Number(ClassicLookup(n->kids[0]->text, ignored) ? 1.0 : 0.0);
    }
    if ((n->a->kind == NK::Builtin) && (n->a->op < B_abs))
        Fail(n, "'%s' is not a function.", n->a->text.c_str());
    if ((n->a->kind == NK::Builtin) && (n->a->text == "trace") && (n->kids.size() == 4))
        return CallTrace(n);
    if (mArgDepth == mArgPool.size())
        mArgPool.emplace_back(new PValues());
    PValues& args = *mArgPool[mArgDepth++];
    args.clear();
    struct Release { size_t& depth; PValues& args; ~Release() { args.clear(); --depth; } } release{mArgDepth, args};
    if (n->a->kind == NK::Builtin)
    {
        EvalArgs(n, args);
        return CallBuiltin(BI(n->a->op), n->a->text, args, n, n);
    }
    PValue callee = AsValue(Eval(n->a), n->a);
    EvalArgs(n, args);
    return AsValue(CallValue(callee, args, n), n);
}

PValue Pov4Evaluator::CallTrace(const Node* n)
{
    const Node* target = n->kids[3];
    if (target->kind != NK::Ident)
        Fail(target, "The normal argument of 'trace' must be a name.");
    PValues args;
    for (size_t i = 0; i < 3; ++i)
        args.push_back(AsValue(Eval(n->kids[i]), n->kids[i]));
    std::string normal = NewName();
    AppendPending("#declare " + normal + " = <0, 0, 0>;", n);
    std::string text = ClassicCallText("trace", args, n);
    text.insert(text.size() - 1, ", " + normal);
    PValue hit = ClassicEval(text, n);
    SymbolTable* global = mParser.mSymbolStack.GetGlobalTable();
    PValue value = FromClassic(global->Find_Symbol(normal.c_str()), normal, false);
    global->Remove_Symbol(normal.c_str(), false, nullptr, 0);
    if (PValue* bound = FindBinding(target))
    {
        *bound = value;
        MarkDirty(target);
    }
    else
        Bind(target->sym, value, false);
    return hit;
}

void Pov4Evaluator::CallEmit(const Node* n, Output& out)
{
    PValue callee;
    if (n->a->kind != NK::Builtin)
        callee = AsValue(Eval(n->a), n->a);
    if ((n->a->kind == NK::Builtin) || (callee.type == PValue::Builtin))
    {
        out.Put(Item(Call(n), n), false);
        return;
    }
    PValues args;
    EvalArgs(n, args);
    if ((callee.type == PValue::Handle) && (callee.As<HandleData>().token == MACRO_ID_TOKEN))
    {
        std::shared_ptr<SyncData> sync;
        if (out.IsBlock())
        {
            sync = std::make_shared<SyncData>(this);
            mSyncs.insert(sync.get());
        }
        ExportBindings(n, &out, sync.get());
        std::string text = ClassicCallText(callee.As<HandleData>().name, args, n);
        size_t mark = mPrinted.size();
        for (const PValue& a : args)
            CollectRefs(a);
        if (out.IsBlock())
        {
            mPrinted.push_back(sync);
            out.Put(TextItem(Item::Raw, text, std::string(), n, mark), false);
        }
        else if (out.IsScene())
        {
            out.Put(TextItem(Item::Raw, text, std::string(), n, mark), false);
            Flush();
            ImportBindings();
        }
        else
        {
            mPrinted.resize(mark);
            PValue result = ClassicEval(text, n);
            ImportBindings();
            out.Put(Item(result, n), false);
        }
        return;
    }
    out.Put(Item(CallValue(callee, args, n), n), false);
}

PValue Pov4Evaluator::CallValue(const PValue& callee, PValues& args, const Node* at)
{
    switch (callee.type)
    {
        case PValue::Fn:
            return CallClosure(callee.As<Closure>(), args, at);
        case PValue::Builtin:
        {
            std::string word;
            if (callee.ref)
                word = callee.As<std::string>();
            for (const auto& kv : BuiltinNames())
                if (word.empty() && (kv.second == BI(callee.v[0])))
                    word = kv.first;
            return CallBuiltin(BI(callee.v[0]), word, args, nullptr, at);
        }
        case PValue::Handle:
        {
            const HandleData& h = callee.As<HandleData>();
            if (h.token == MACRO_ID_TOKEN)
            {
                ExportBindings(at, nullptr, nullptr);
                PValue result = ClassicEval(ClassicCallText(h.name, args, at), at);
                ImportBindings();
                return result;
            }
            if ((h.token == FUNCT_ID_TOKEN) || (h.token == VECTFUNCT_ID_TOKEN) || (h.token == SPLINE_ID_TOKEN))
                return ClassicEval(ClassicCallText(h.name, args, at), at);
            Fail(at, "A %s cannot be called.", TypeName(callee).c_str());
        }
        default:
            Fail(at, "A %s cannot be called.", TypeName(callee).c_str());
    }
}

PValue Pov4Evaluator::CallClosure(const Closure& c, PValues& args, const Node* at)
{
    const std::vector<const Node*>& params = c.params->kids;
    if (args.size() > params.size())
        Fail(at, "Too many arguments: %u given, %u expected.", unsigned(args.size()), unsigned(params.size()));
    DepthGuard guard(*this, at);
    DepthGuard calls(*this, mCalls, kMaxCalls, "Function calls nested", at);
    std::shared_ptr<Scope> saved = mScope;
    mScope = std::make_shared<Scope>(c.env);
    ++mEpoch;
    struct Restore
    {
        Pov4Evaluator& ev;
        std::shared_ptr<Scope>& saved;
        ~Restore() { ev.mScope = saved; }
    } restore{*this, saved};
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (i < args.size())
            Bind(params[i]->sym, Store(args[i], at), false);
        else if (params[i]->a != nullptr)
            Bind(params[i]->sym, Store(Eval(params[i]->a), params[i]->a), false);
        else
            Fail(at, "Missing argument '%s'.", mSymbolNames[params[i]->sym].c_str());
    }
    if (c.expressionBody)
        return Eval(c.body);
    ListOutput out(*this);
    Flow f = ExecItems(c.body->kids, out);
    if ((f == Flow::Break) || (f == Flow::Continue))
        Fail(at, "'break' or 'continue' outside a loop.");
    PValue result;
    if (f == Flow::Return)
    {
        result = mReturn;
        mReturn = PValue();
        if (!mReturnBare)
        {
            if (!out.items.empty())
                Fail(at, "A function returned a value after emitting items.");
            return result;
        }
        mReturnBare = false;
    }
    result = PValue();
    result.type = PValue::Frag;
    result.ref = NewContainer<Items>(std::move(out.items));
    return result;
}

PValue Pov4Evaluator::CallBuiltin(BI id, const std::string& word, PValues& args, const Node* call, const Node* at)
{
    auto argc = [&](size_t lo, size_t hi) {
        if ((args.size() < lo) || (args.size() > hi))
            Fail(at, "'%s' takes %u to %u arguments, %u given.", word.c_str(), unsigned(lo), unsigned(hi), unsigned(args.size()));
    };
    auto f = [&](size_t i) {
        if (args[i].type == PValue::Num)
            return args[i].v[0];
        return Float(args[i], at, ("Argument " + std::to_string(i + 1) + " of '" + word + "'").c_str());
    };
    auto vec = [&](const Vector3d& v) { double e[3] = {v[X], v[Y], v[Z]}; return PValue::Vector(e, 3); };
    auto num = [](double d) { return PValue::Number(d); };
    auto integer = [&](double d) {
        if (!(d > -2147483649.0) || !(d < 2147483648.0))
            Fail(at, "%g is outside the integer range of '%s'.", d, word.c_str());
        return int(d);
    };
    for (PValue& a : args)
        if (a.type == PValue::Frag)
            a = AsValue(a, at);
    double val;
    switch (id)
    {
        case B_x: { double e[3] = {1, 0, 0}; return PValue::Vector(e, 3); }
        case B_y: { double e[3] = {0, 1, 0}; return PValue::Vector(e, 3); }
        case B_z: { double e[3] = {0, 0, 1}; return PValue::Vector(e, 3); }
        case B_t: { double e[4] = {0, 0, 0, 1}; return PValue::Vector(e, 4); }
        case B_u: { double e[2] = {1, 0}; return PValue::Vector(e, 2); }
        case B_v: { double e[2] = {0, 1}; return PValue::Vector(e, 2); }
        case B_pi: return num(M_PI);
        case B_tau: return num(M_TAU);
        case B_clock: return num(mParser.clockValue);
        case B_clock_on: return num(double(mParser.useClock));
        case B_yes: case B_on: return num(1.0);
        case B_no: case B_off: return num(0.0);
        case B_version: return num(mScene.EffectiveLanguageVersion() / 100.0);
        case B_now:
        {
            using FractionalDays = std::chrono::duration<double, std::ratio<24 * 60 * 60>>;
            return num(std::chrono::duration_cast<FractionalDays>(std::chrono::system_clock::now() - mParser.mY2K).count());
        }
        case B_abs: argc(1, 1); return num(std::fabs(f(0)));
        case B_acos: case B_asin:
            argc(1, 1);
            val = f(0);
            if ((val > 1.0) || (val < -1.0))
            {
                mParser.Warning("Domain error in %s.", word.c_str());
                val = (val > 1.0) ? 1.0 : -1.0;
            }
            return num((id == B_acos) ? std::acos(val) : std::asin(val));
        case B_acosh: argc(1, 1); return num(std::acosh(f(0)));
        case B_asinh: argc(1, 1); return num(std::asinh(f(0)));
        case B_atan: argc(1, 1); return num(std::atan(f(0)));
        case B_atanh: argc(1, 1); return num(std::atanh(f(0)));
        case B_atan2:
            argc(2, 2);
            if (!FTrue(f(0)) && !FTrue(f(1)))
                Fail(at, "Domain error in atan2!");
            return num(std::atan2(f(0), f(1)));
        case B_ceil: argc(1, 1); return num(std::ceil(f(0)));
        case B_cos: argc(1, 1); return num(std::cos(f(0)));
        case B_cosh: argc(1, 1); return num(std::cosh(f(0)));
        case B_degrees: argc(1, 1); return num(f(0) / M_PI_180);
        case B_radians: argc(1, 1); return num(f(0) * M_PI_180);
        case B_div:
            argc(2, 2);
            if (f(1) == 0.0)
                Fail(at, "Divide by zero.");
            return num(double(integer(f(0) / f(1))));
        case B_exp: argc(1, 1); return num(std::exp(f(0)));
        case B_floor: argc(1, 1); return num(std::floor(f(0)));
        case B_int: argc(1, 1); return num(double(integer(f(0))));
        case B_ln: case B_log:
            argc(1, 1);
            val = f(0);
            if (val <= 0.0)
                Fail(at, "%s of negative number %lf", word.c_str(), val);
            return num((id == B_ln) ? std::log(val) : std::log10(val));
        case B_max: case B_min:
            if (args.empty())
                argc(1, 1);
            val = f(0);
            for (size_t i = 1; i < args.size(); ++i)
                val = (id == B_max) ? std::max(val, f(i)) : std::min(val, f(i));
            return num(val);
        case B_mod: argc(2, 2); return num(std::fmod(f(0), f(1)));
        case B_pow:
            argc(2, 2);
            if ((f(0) == 0.0) && (f(1) == 0.0))
                Fail(at, "Domain error.");
            return num(std::pow(f(0), f(1)));
        case B_select:
            argc(3, 4);
            val = f(0);
            if (args.size() == 4)
                return num((val < 0.0) ? f(1) : (val == 0.0) ? f(2) : f(3));
            return num((val < 0.0) ? f(1) : f(2));
        case B_sin: argc(1, 1); return num(std::sin(f(0)));
        case B_sinh: argc(1, 1); return num(std::sinh(f(0)));
        case B_sqrt:
            argc(1, 1);
            if (f(0) < 0.0)
                Fail(at, "sqrt of negative number %lf", f(0));
            return num(std::sqrt(f(0)));
        case B_sqr: argc(1, 1); val = f(0); return num(val * val);
        case B_tan: argc(1, 1); return num(std::tan(f(0)));
        case B_tanh: argc(1, 1); return num(std::tanh(f(0)));
        case B_vdot: argc(2, 2); return num(dot(Vec3(args[0], at), Vec3(args[1], at)));
        case B_vlength: argc(1, 1); return num(Vec3(args[0], at).length());
        case B_vcross: argc(2, 2); return vec(cross(Vec3(args[0], at), Vec3(args[1], at)));
        case B_vnormalize:
        {
            argc(1, 1);
            Vector3d v = Vec3(args[0], at);
            if ((v[X] == 0.0) && (v[Y] == 0.0) && (v[Z] == 0.0))
            {
                if (mScene.EffectiveLanguageVersion() >= 350)
                    mParser.PossibleError("Normalizing zero-length vector.");
                return vec(Vector3d(0.0, 0.0, 0.0));
            }
            return vec(v.normalized());
        }
        case B_vrotate:
        {
            argc(2, 2);
            TRANSFORM trans;
            Vector3d result;
            Compute_Rotation_Transform(&trans, Vec3(args[1], at));
            MTransPoint(result, Vec3(args[0], at), &trans);
            return vec(result);
        }
        case B_vaxis_rotate:
        {
            argc(3, 3);
            TRANSFORM trans;
            Vector3d result;
            Vector3d point = Vec3(args[0], at);
            Vector3d axis = Vec3(args[1], at);
            Compute_Axis_Rotation_Transform(&trans, axis, f(2) * M_PI_180);
            MTransPoint(result, point, &trans);
            return vec(result);
        }
        case B_strlen: argc(1, 1); return num(double(Str(args[0], at).size()));
        case B_strcmp: argc(2, 2); return num(double(UCS2_strcmp(Str(args[0], at).c_str(), Str(args[1], at).c_str())));
        case B_asc: argc(1, 1); return num(Str(args[0], at).empty() ? 0.0 : double(Str(args[0], at)[0]));
        case B_val: argc(1, 1); return num(std::atof(UCS2toSysString(Str(args[0], at)).c_str()));
        case B_concat:
        {
            UCS2String s;
            for (const PValue& a : args)
                s += Str(a, at);
            return MakeString(s);
        }
        case B_chr: argc(1, 1); return MakeString(UCS2String(1, UCS2(integer(f(0)))));
        case B_bitwise_and: case B_bitwise_or: case B_bitwise_xor:
        {
            if (args.empty())
                argc(1, 1);
            int r = integer(f(0));
            for (size_t i = 1; i < args.size(); ++i)
                r = (id == B_bitwise_and) ? (r & integer(f(i))) : (id == B_bitwise_or) ? (r | integer(f(i))) : (r ^ integer(f(i)));
            return num(double(r));
        }
        case B_dimensions: case B_dimension_size:
            if (!args.empty() && (args[0].type == PValue::Arr))
            {
                int sizes[kMaxDims];
                int dims = ArrayShape(args[0].As<PValues>(), sizes);
                if (id == B_dimensions)
                    return num(double(dims));
                argc(2, 2);
                int d = integer(f(1));
                if ((d >= 1) && (d <= dims))
                    return num(double(sizes[d - 1]));
                mParser.Warning("Querying size of dimension %d in %d-dimensional array.", d, dims);
                return num(0.0);
            }
            break;
        case B_seed:
            argc(1, 1);
            return num(double(mParser.stream_seed(integer(f(0)))));
        case B_rand:
        {
            argc(1, 1);
            int stream = integer(f(0));
            if ((stream < 0) || (unsigned(stream) >= mParser.Number_Of_Random_Generators))
                Fail(at, "Illegal random number generator.");
            return num(mParser.stream_rand(stream));
        }
        case B_len:
            argc(1, 1);
            switch (args[0].type)
            {
                case PValue::Arr:  return num(double(args[0].As<PValues>().size()));
                case PValue::Str:  return num(double(args[0].As<StrData>().s.size()));
                case PValue::Dict: return num(double(args[0].As<DictData>().items.size()));
                case PValue::Frag: return num(double(args[0].As<Items>().size()));
                case PValue::Null: return num(0.0);
                default: Fail(at, "A %s has no length.", TypeName(args[0]).c_str());
            }
        case B_range:
        {
            argc(2, 3);
            double a = f(0), b = f(1), s = (args.size() > 2) ? f(2) : 1.0;
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(s))
                Fail(at, "'range' needs finite bounds and step.");
            if (std::fabs(s) < EPSILON)
                Fail(at, "The step of 'range' must be non-zero.");
            PValues out;
            if ((b - a) / s > double(kMaxArray))
                Fail(at, "'range' would exceed %u elements.", unsigned(kMaxArray));
            if (((s > 0) && (a < b + EPSILON)) || ((s < 0) && (a > b - EPSILON)))
            {
                out.reserve(size_t((b - a) / s) + 2);
                for (double x = a; !(((s > 0) && (x > b + EPSILON)) || ((s < 0) && (x < b - EPSILON))); x += s)
                {
                    if ((x + s == x) || (out.size() >= kMaxArray))
                        Fail(at, "'range' would not end or would exceed %u elements.", unsigned(kMaxArray));
                    out.push_back(num(x));
                }
            }
            return MakeArray(std::move(out));
        }
        case B_map: case B_filter:
        {
            argc(2, 2);
            if (args[0].type != PValue::Arr)
                Fail(at, "'%s' needs an array, not a %s.", word.c_str(), TypeName(args[0]).c_str());
            PValue fn = args[1];
            PValues out;
            out.reserve((id == B_map) ? args[0].As<PValues>().size() : 0);
            for (const PValue& e : args[0].As<PValues>())
            {
                PValues one{e};
                PValue r = AsValue(CallValue(fn, one, at), at);
                if (id == B_map)
                    out.push_back(Store(r, at));
                else if (Truth(r, at))
                    out.push_back(e);
            }
            return MakeArray(std::move(out));
        }
        case B_push:
        {
            argc(2, 2);
            if (args[0].type != PValue::Arr)
                Fail(at, "'push' needs an array, not a %s.", TypeName(args[0]).c_str());
            const PValues& in = args[0].As<PValues>();
            if (in.size() >= kMaxArray)
                Fail(at, "An array cannot have more than %u elements.", unsigned(kMaxArray));
            PValues out;
            out.reserve(in.size() + 1);
            out.insert(out.end(), in.begin(), in.end());
            out.push_back(Store(args[1], at));
            return MakeArray(std::move(out));
        }
        case B_keys:
        {
            argc(1, 1);
            if (args[0].type != PValue::Dict)
                Fail(at, "'keys' needs a dictionary, not a %s.", TypeName(args[0]).c_str());
            PValues out;
            out.reserve(args[0].As<DictData>().items.size());
            for (const auto& kv : args[0].As<DictData>().items)
                out.push_back(MakeString(UTF8toUCS2String(kv.first)));
            return MakeArray(std::move(out));
        }
        case B_array:
        {
            argc(1, 2);
            double n = f(0);
            if (!(n >= 0) || !(n <= double(kMaxArray)))
                Fail(at, "An array size must be between 0 and %u.", unsigned(kMaxArray));
            return MakeArray(PValues(size_t(n), (args.size() > 1) ? Store(args[1], at) : PValue()));
        }
        case B_debug: case B_warning: case B_error:
        {
            argc(1, 1);
            std::string text = UCS2toSysString(Str(args[0], at));
            if (id == B_error)
                Fail(at, "%s", text.c_str());
            Flush();
            if (id == B_warning)
                mParser.Warning("%s", text.c_str());
            else
            {
                if (text.size() > 200)
                    text = text.substr(0, 156) + "...";
                mParser.Debug_Info("%s", text.c_str());
            }
            return PValue();
        }
        case B_defined:
            Fail(at, "'defined' takes one name.");
        default:
            break;
    }
    if (args.empty() && (call == nullptr))
        return ClassicEval(word, at);
    return ClassicEval(ClassicCallText(word, args, at), at);
}

//------------------------------------------------------------------------------

void Pov4Evaluator::Run()
{
    if (!mScene.pov4LoweredFile.empty())
    {
        mDump.reset(new std::ofstream(UCS2toSysString(mScene.pov4LoweredFile).c_str(), std::ios::binary));
        if (!*mDump)
            mParser.Error("Cannot write the lowered SDL file '%s'.", UCS2toSysString(mScene.pov4LoweredFile).c_str());
    }
    if (!mScene.headerFile.empty())
        Snippet("#include " + EscapeString(mScene.headerFile) + "\n", {1}, 0);
    std::string version = "#version ";
    AppendNumber(version, mScene.pov4Version / 100.0);
    Snippet(version + ";\n", {1}, 0);
    Program& main = Load(mScene.inputFile, POV_File_Text_POV, nullptr);
    SceneOutput scene(*this);
    if (ExecItems(main.root->kids, scene) != Flow::Normal)
        Fail(main.root, "'break', 'continue' or 'return' outside a loop or function.");
    Flush();
}

namespace
{

struct StackRun
{
    std::function<void()> body;
    std::exception_ptr error;
#if defined(_WIN32)
    void* caller = nullptr;
#elif defined(__linux__)
    ucontext_t caller;
#endif
    void Go() { try { body(); } catch (...) { error = std::current_exception(); } }
};

#if defined(_WIN32)
void CALLBACK StackRunMain(void* run)
{
    static_cast<StackRun*>(run)->Go();
    SwitchToFiber(static_cast<StackRun*>(run)->caller);
}
#elif defined(__linux__)
thread_local StackRun* tStackRun = nullptr;
void StackRunMain() { tStackRun->Go(); }
#endif

void RunOnEvaluatorStack(std::function<void()> body)
{
    StackRun run;
    run.body = std::move(body);
#if defined(_WIN32)
    run.caller = ConvertThreadToFiber(nullptr);
    bool converted = (run.caller != nullptr);
    if (!converted && (GetLastError() == ERROR_ALREADY_FIBER))
        run.caller = GetCurrentFiber();
    void* fiber = (run.caller != nullptr) ? CreateFiberEx(0, kEvaluatorStack, 0, StackRunMain, &run) : nullptr;
    if (fiber != nullptr)
    {
        SwitchToFiber(fiber);
        DeleteFiber(fiber);
    }
    else
        run.Go();
#if (_WIN32_WINNT >= 0x0501)
    if (converted)
        ConvertFiberToThread();
#endif
#elif defined(__linux__)
    void* stack = mmap(nullptr, kEvaluatorStack, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    ucontext_t context;
    if ((stack == MAP_FAILED) || (mprotect(stack, 65536, PROT_NONE) != 0) || (getcontext(&context) != 0))
        run.Go();
    else
    {
        context.uc_stack.ss_sp = stack;
        context.uc_stack.ss_size = kEvaluatorStack;
        context.uc_link = &run.caller;
        makecontext(&context, StackRunMain, 0);
        StackRun* outer = tStackRun;
        tStackRun = &run;
        swapcontext(&run.caller, &context);
        tStackRun = outer;
    }
    if (stack != MAP_FAILED)
        munmap(stack, kEvaluatorStack);
#else
    run.Go();
#endif
    if (run.error)
        std::rethrow_exception(run.error);
}

}
// end of anonymous namespace

void Parser::Run_Pov4()
{
    mHadCamera = false;
    RunOnEvaluatorStack([this]() {
        Pov4Evaluator evaluator(*this);
        evaluator.Run();
    });
    if (!mHadCamera && sceneData->parseFilterTags.specified)
        mMessageFactory.Info("No camera survived Filter_Tags; using the default camera.");
}

}
// end of namespace pov_parser
