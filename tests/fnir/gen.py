#!/usr/bin/env python3
"""Random classic-function scenes for differential renders of two builds.
usage: gen.py SEED OUT.pov -> writes the scene, prints its consumer kind and the constructs it uses.
The same seed always writes the same scene."""
import random, sys

seed = int(sys.argv[1]); out = sys.argv[2]
R = random.Random(seed)
tags = set()

# functions.inc wrappers: name, extra params beyond x,y,z (with safe ranges)
INC = [("f_noise3d", []), ("f_snoise3d", []), ("f_sphere", [(0.3, 1.5)]), ("f_torus", [(0.8, 1.2), (0.2, 0.4)]),
       ("f_noise_generator", [(1, 3)]), ("f_bozo", []), ("f_dents", []), ("f_granite", []),
       ("f_ridged_mf", [(0.5, 1.5), (1.5, 3.0), (2, 5), (0.5, 1.0), (1.0, 2.0), (1, 3)]),
       ("f_rounded_box", [(0.05, 0.2), (0.5, 1), (0.5, 1), (0.5, 1)]),
       ("f_agate", []), ("f_checker", []), ("f_gradientX", []), ("f_wrinkles", []), ("f_ph", []), ("f_r", []), ("f_th", []),
       ("f_helix1", [(1, 3), (0.1, 0.3), (0.05, 0.2), (0.3, 0.7), (1, 2), (0.5, 1.5), (0, 1)])]

def num():
    c = R.random()
    if c < 0.15: return R.choice(["0", "1", "2", "0.5", "-1", "3"])
    if c < 0.2: return R.choice(["-0", "1e-30", "1e30"])
    return "%.6g" % R.uniform(-3, 3)

class Fn:
    def __init__(s, name, params, kind="float"):
        s.name, s.params, s.kind = name, params, kind

funcs = []   # user functions declared so far

def call_args(n, vars_, depth):
    return ", ".join(expr(vars_, depth + 1) for _ in range(n))

def leaf(vars_):
    if vars_ and R.random() < 0.7:
        return R.choice(vars_)
    return num()

def expr(vars_, depth):
    if depth > R.choice([2, 3, 4, 5]) or R.random() < 0.12:
        return leaf(vars_)
    c = R.random()
    e = lambda: expr(vars_, depth + 1)
    if c < 0.22:
        return "(%s %s %s)" % (e(), R.choice(["+", "-", "*", "*", "/"]), e())
    if c < 0.30:
        tags.add("cmp")
        return "(%s %s %s)" % (e(), R.choice([">", "<", "=", "!=", "<=", ">=", "&", "|"]), e())
    if c < 0.36:
        tags.add("select")
        if R.random() < 0.5:
            return "select(%s, %s, %s)" % (e(), e(), e())
        return "select(%s, %s, %s, %s)" % (e(), e(), e(), e())
    if c < 0.42:
        tags.add("minmax")
        return "%s(%s)" % (R.choice(["min", "max"]), ", ".join(e() for _ in range(R.randint(2, 4))))
    if c < 0.50:
        f = R.choice(["abs", "sin", "cos", "sqrt", "floor", "ceil", "int", "exp", "atan", "tanh", "radians", "degrees"])
        tags.add("math1")
        if f == "sqrt": return "sqrt(abs(%s))" % e()
        if f == "exp": return "exp(-abs(%s))" % e()
        return "%s(%s)" % (f, e())
    if c < 0.54:
        tags.add("math2")
        f = R.choice(["mod", "atan2", "pow", "div"])
        if f == "pow": return "pow(abs(%s) + 0.1, %s)" % (e(), R.choice(["1.5", "0.5", "2", "3", e()]))
        if f == "div": return "div(%s, %s)" % (e(), R.choice(["0.7", "2", "-1.3"]))
        return "%s(%s, %s)" % (f, e(), e())
    if c < 0.57:
        tags.add("neg"); return "-(%s)" % e()
    if c < 0.62 and depth < 3:
        tags.add("sum")
        v = "i%d" % depth
        return "%s(%s, 1, %d, %s)" % (R.choice(["sum", "prod"]), v, R.randint(1, 3), expr(vars_ + [v], depth + 2))
    if c < 0.74:
        name, extra = R.choice(INC)
        tags.add("inc:" + name)
        xs = [expr(vars_, depth + 2) if R.random() < 0.6 else R.choice(vars_ or ["0.5"]) for _ in range(3)]
        ex = []
        for lo, hi in extra:
            ex.append(str(R.randint(int(lo), int(hi))) if isinstance(lo, int) else "%.3g" % R.uniform(lo, hi))
        return "%s(%s)" % (name, ", ".join(xs + ex))
    if funcs:
        f = R.choice(funcs) if R.random() < 0.6 else funcs[-1]
        if f.kind == "float":
            tags.add("call")
            return "%s(%s)" % (f.name, call_args(len(f.params), vars_, depth))
        tags.add("vcall:" + f.kind)
        n = 1 if f.kind == "spline" else 3
        mem = {"pigment": ["red", "green", "blue", "gray", "filter", "transmit", "x", "t"],
               "transform": ["x", "y", "z"], "spline": ["x", "y"]}[f.kind]
        return "%s(%s).%s" % (f.name, call_args(n, vars_, depth), R.choice(mem))
    return leaf(vars_)

decls = []
nfun = R.randint(1, 8)
PN = ["pa", "pb", "pc", "pd", "x", "y", "z"]
for k in range(nfun):
    c = R.random()
    name = "F%d" % k
    if c < 0.07:
        decls.append("#declare %s = function { pattern { %s scale %.3g } }" % (name, R.choice(["bozo", "agate", "granite", "wrinkles", "checker", "gradient x", "leopard", "ripples"]), R.uniform(0.2, 1)))
        funcs.append(Fn(name, ["x", "y", "z"])); tags.add("patternfn"); continue
    if c < 0.12:
        decls.append("#declare %s = function { pigment { %s color_map { [0 rgbft <0.1,0.2,0.3,0.1,0>] [1 rgbft <0.9,0.6,0.2,0,0.3>] } scale %.3g } }" % (name, R.choice(["bozo", "gradient y", "granite", "marble"]), R.uniform(0.2, 1)))
        funcs.append(Fn(name, [], "pigment")); continue
    if c < 0.15:
        decls.append("#declare %s = function { transform { rotate <%d,%d,%d> scale %.3g translate <%.2g,0,0> } }" % (name, R.randint(0, 90), R.randint(0, 90), R.randint(0, 90), R.uniform(0.5, 2), R.uniform(-1, 1)))
        funcs.append(Fn(name, [], "transform")); continue
    if c < 0.17:
        decls.append("#declare %s = function { spline { linear_spline 0, <0,0.1>, 1, <1,0.5>, 2, <0.2,1>, 3, <0.7,0.3> } }" % name)
        funcs.append(Fn(name, [], "spline")); continue
    if c < 0.35:
        body = expr(["x", "y", "z"], 0)
        decls.append("#declare %s = function { %s }" % (name, body))
        funcs.append(Fn(name, ["x", "y", "z"]))
        continue
    np = R.randint(1, 5)
    params = R.sample(PN, np)
    body = expr(params, 0)
    decls.append("#declare %s = function(%s) { %s }" % (name, ", ".join(params), body))
    funcs.append(Fn(name, params))

main = expr(["x", "y", "z"], 0)
kind = R.choices(["pigment", "isosurface", "media", "parametric", "pigment-batch"], [5, 2, 2, 1, 2])[0]
cam = "camera { perspective location <0,0,-4> look_at <0,0,0> right x*4/3 up y angle 40 }"
with open(out, "w") as f:
    f.write("#version %s;\n#include \"functions.inc\"\nglobal_settings { assumed_gamma 1.0 }\n" % R.choice(["3.7", "3.7", "3.8"]))
    f.write("\n".join(decls) + "\n")
    if kind == "pigment":
        f.write("plane { z, 0 pigment { function { %s } color_map { [0 rgb 0][0.5 rgb <1,0.3,0>][1 rgb 1] } } finish { ambient 1 diffuse 0 } }\n" % main)
    elif kind == "pigment-batch":
        # a smooth function pattern inside media density evaluates through the batch entry
        f.write("box { -1.5, 1.5 pigment { rgbt 1 } hollow interior { media { emission 0.6 absorption 0.3 method 3 intervals 1 samples 6 "
                "density { function { %s } color_map { [0 rgb 0][1 rgb <1,0.5,0.2>] } } } } }\n" % main)
        f.write("background { rgb <0.1,0.1,0.2> }\n")
    elif kind == "media":
        f.write("box { -1.5, 1.5 pigment { rgbt 1 } hollow interior { media { emission 0.5 method 3 intervals 2 samples 4 "
                "density { function { abs(%s) } } } } }\n" % main)
    elif kind == "isosurface":
        f.write("isosurface { function { sqrt(x*x + y*y + z*z) - 1 + 0.15*sin(%s) } contained_by { box { -2, 2 } } "
                "max_gradient %d accuracy 0.01 pigment { rgb <0.8,0.6,0.3> } finish { ambient 0.3 } }\n" % (main, R.choice([3, 5, 10])))
        f.write("light_source { <3,4,-5> rgb 1 }\n")
    else:
        f.write("parametric { function { cos(u)*(1 + 0.1*sin(%s)) } function { sin(u)*(1 + 0.1*sin(%s)) } function { v } "
                "<0,-1>, <2*pi,1> contained_by { box { -1.5, 1.5 } } max_gradient 3 accuracy 0.01 precompute 10 x,y,z "
                "pigment { rgb <0.3,0.8,0.5> } finish { ambient 0.4 } rotate x*30 }\n" % (expr(["u", "v"], 0), expr(["u", "v"], 0)))
        f.write("light_source { <3,4,-5> rgb 1 }\n")
    f.write(cam + "\n")
tags.add("kind:" + kind)
print(kind, " ".join(sorted(tags)))
