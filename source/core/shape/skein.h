//******************************************************************************
///
/// @file core/shape/skein.h
///
/// Declarations related to the skein geometric primitive.
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

#ifndef POVRAY_CORE_SKEIN_H
#define POVRAY_CORE_SKEIN_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ standard header files
#include <functional>
#include <memory>
#include <string>
#include <vector>

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/math/matrix.h"
#include "core/scene/object.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCoreShape
///
/// @{

/// A series through values or space: cubic Bézier segments from plain points, points with handles, or a mix.
struct SkeinPath final
{
    enum Interpolation { kLinear, kQuadratic, kCubic, kNatural };
    struct Point { Vector3d point, in, out; DBL parameter = 0.0; bool haveParameter = false, haveIn = false, haveOut = false; };

    int dimension = 1;
    bool closed = false;
    /// Points were given as (u, v, d) on the surface and are placed in space at Prepare.
    bool fromSurface = false;
    bool useArc = false;
    Interpolation interp = kLinear;
    std::vector<Point> raw;
    std::vector<DBL> knots;
    std::vector<Vector3d> control;
    std::vector<DBL> params, lengths, speeds;
    DBL total = 0.0;

    DBL Start() const { return knots.front(); }
    DBL End() const { return knots.back(); }
    /// Fits the segments through `points`; returns an error message, empty on success.
    std::string Build(const std::vector<Point>& points, Interpolation interpolation);
    /// Reparameterises by arc length, keeping the parameter range.
    void UseArcLength();
    /// The point and its first two derivatives at `t`, wrapped when closed and clamped otherwise.
    void Evaluate(DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const;
    /// A box holding the path over [ta, tb], from the control points of the pieces in that range.
    void Bound(DBL ta, DBL tb, Vector3d& lo, Vector3d& hi) const;
    /// True when the path is open, linear and through collinear points without handles: the line from `origin` along `direction`.
    bool Straight(Vector3d& origin, Vector3d& direction) const;

    private:
        void Piece(int i, DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const;
        void Segment(DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const;
        DBL ArcParameter(DBL t, DBL& d1, DBL& d2) const;
        void Hull(DBL x0, DBL x1, Vector3d& lo, Vector3d& hi) const;
};

/// A greyscale image for the `image` modifier, read and interpolated as `image_pattern` reads it, with min and max pyramids for bounds.
struct SkeinImage final
{
    int width = 0, height = 0;
    bool bicubic = false;
    std::vector<float> grey;
    std::vector<std::vector<float>> lows, highs;

    /// Builds the pyramids once `grey` is filled.
    void Build();
    /// The value at (x, y) in 0..1 per tile, with its slopes in x and y.
    DBL Sample(DBL x, DBL y, DBL& dx, DBL& dy) const;
    /// Encloses the value over a box of (x, y).
    void Range(DBL x0, DBL x1, DBL y0, DBL y1, DBL& lo, DBL& hi) const;
};

/// A chain of built-in modifiers over named inputs, in postfix order.
struct SkeinMap final
{
    enum Op : unsigned char { kInput, kLinear, kSin, kCos, kLength, kAtan2, kRange, kPath, kNoise, kFbm, kCells, kImage };
    enum Method : unsigned char { kPlain, kRepeat, kMirror, kClamp };
    struct Node
    {
        Op op = kInput;
        unsigned char input = 0;
        Method method = kPlain;
        DBL a = 0.0, b = 0.0, c = 0.0, d = 0.0;
        int count = 0;
        std::shared_ptr<const SkeinPath> path;
        std::shared_ptr<const SkeinImage> image;
    };
    static const int kMaxDepth = 8;

    std::vector<Node> nodes;
    unsigned int inputs = 0;

    static int Arity(Op op) { return op == kInput ? 0 : (op == kNoise) || (op == kFbm) ? 3 : (op == kLength) || (op == kAtan2) || (op == kCells) || (op == kImage) ? 2 : 1; }
};

struct SkeinSum;

/// A step parameter: a constant, a function whose parameters name skein inputs, a map, a scalar path of v, or a sum of these.
struct SkeinValue final
{
    enum Kind : unsigned char { kConstant, kFunction, kMap, kPath, kSum };
    enum Input : unsigned char { kU, kV, kX, kY, kZ, kNX, kNY, kNZ, kInputs };

    Kind kind = kConstant;
    DBL constant = 0.0;
    std::shared_ptr<GenericScalarFunction> function;
    std::vector<unsigned char> inputs;
    std::shared_ptr<const SkeinMap> map;
    std::shared_ptr<const SkeinPath> path;
    std::shared_ptr<SkeinSum> sum;
    int slot = -1;

    SkeinValue() = default;
    SkeinValue(const SkeinValue& other) { *this = other; }
    SkeinValue& operator=(const SkeinValue& other);

    bool Varies() const { return kind != kConstant; }
    bool ReadsNormal() const;
};

/// The entries of a `sum`, `product`, `min` or `max` value, each evaluated at the same inputs; copied deeply so slots stay per skein.
struct SkeinSum final
{
    enum Op : unsigned char { kAdd, kMultiply, kMin, kMax };
    Op op = kAdd;
    std::vector<SkeinValue> entries;
};

/// The axis of an extrude or an axial step: a straight line, a vector path or three functions of t, with its rotation-minimising frame table.
/// `source` holds a sample_path as written, so each Prepare places it against its own surface; `drift` and `twist` measure a target against the axis it follows.
struct SkeinAxis final
{
    std::shared_ptr<const SkeinPath> path;
    std::shared_ptr<const SkeinPath> source;
    std::shared_ptr<GenericScalarFunction> functions[3];
    Vector3d direction = Vector3d(0.0, 1.0, 0.0), e1, e2;
    DBL t0 = 0.0, t1 = 1.0, correction = 0.0, turning = 0.0, length = 0.0, drift = 0.0, twist = 0.0;
    bool closed = false, line = false;
    std::vector<Vector3d> reference;
    std::vector<DBL> arc, rates;

    /// Arc length from the start of the axis to `t`, with its rate ds/dt.
    DBL Distance(DBL t, DBL& rate) const;
    /// The parameter at arc length `s`, with its rate dt/ds: wrapped when the axis closes, clamped at an open end.
    DBL Parameter(DBL s, DBL& rate) const;
};

/// A curl's roll: the pivot and travel as parsed, and once prepared the frame (T, A, N toward the pivot) and the section's arc-length
/// table, whose parameter is the roll angle; immutable after Prepare.
struct SkeinRoll final
{
    std::shared_ptr<const SkeinPath> source, travel;
    Vector3d foot, T, A, N, start, tail;
    DBL radius = 0.0, step = 0.0, speedBound = 0.0, turnBound = 0.0;
    bool ready = false;
    SkeinAxis table;
    std::vector<Vector3d> section;
    std::vector<Vector2d> normals;

    /// The rolled section C at roll angle `phi`, the travel read at phi / 2pi turns, in (T, A, N) from the foot, with C' and C'' in c[1] and c[2];
    /// returns the radius there.
    DBL Section(DBL phi, Vector3d c[3]) const;
};

/// A `fold`'s solve: as parsed, and once prepared a table of columns of entries (folded point, tangent-plane rotation as three rows)
/// solved from `from` along its row then each column; `exact` when every loop across the columns closes, `gaps` how far each fails.
struct SkeinFold final
{
    Vector2d from;
    bool perturb = false, ready = false, exact = true;
    int columns = 0, rows = 0, bands = 0;
    DBL scale = 1.0, gap = 0.0, tolerance = 0.0;
    std::vector<Vector3d> points, turns;
    std::vector<DBL> gaps;
};

struct SkeinBlend;

/// One map of the unit square's image.
struct SkeinStep final
{
    enum Kind { kAffine, kFold, kDisplace, kTranslate, kScale, kRotate, kBend, kSample, kEnvelope, kBlend, kAxialStep, kCurl, kConform };
    enum Role { kRadius = 0, kArc = 2, kAmount = 0, kAngle = 0, kLimit = 1, kShift = 1, kRadial = 2, kThickness = 0, kAtU = 0, kAtV = 1, kDriver = 0 };
    enum Edge : unsigned char { kNoEdge, kRoundEdge, kFlatEdge };

    Kind kind = kAffine;
    TRANSFORM affine;
    Vector3d axis, along, side, offset, origin;
    SkeinValue value[3];
    std::shared_ptr<SkeinAxis> curve;
    /// The path an axial step follows, its `along`: the axis is then a SkeinAxis too, straight ones included.
    std::shared_ptr<SkeinAxis> target;
    std::shared_ptr<SkeinBlend> blend;
    std::shared_ptr<const SkeinRoll> roll;
    /// The solve of a `fold` (kind kConform; `value` holds its x, y and z).
    std::shared_ptr<const SkeinFold> fold;
    bool normal = false;
    bool alongV = false;
    Edge edge = kNoEdge;
    unsigned char wrapIn = 0, wrapOut = 0;

    SkeinStep() = default;
    SkeinStep(const SkeinStep& other) { *this = other; }
    SkeinStep& operator=(const SkeinStep& other);

    /// Sets a straight hinge's frame from its direction: `side` along it, `axis` z made square to it (x when it runs along z), `along` = side x axis.
    void Hinge(const Vector3d& direction);
};

/// The entries of an `expression_map`, sorted by value; entries made from one declared group share an item number.
struct SkeinBlend final
{
    struct Entry { DBL value; int item; std::vector<SkeinStep> steps; };
    std::vector<Entry> entries;
};

/// The parsed chain and topology, plus the patch bounds and caps that Skein::Prepare derives from them.
struct SkeinData final
{
    enum End { kOpen, kFlat, kPole, kSealed };

    struct Patch { DBL u0, u1, v0, v1, flat; Vector3d lo, hi; int ranges; };
    struct Node { Vector3d lo, hi; int child[2]; int patch; };
    struct Cap { bool present = false; DBL v = 0.0, reach = 0.0, turn = 1.0; Vector3d centre, normal, e1, e2; std::vector<DBL> angles; };

    static const int kMaxFunctions = 32;

    std::vector<SkeinStep> steps;
    std::vector<const SkeinValue *> slots;
    int functionCount = 0;
    bool readsNormal = false, resamples = false;
    int lastNormal = -1;
    bool wrapU = false, wrapV = false;
    End ends[2] = { kOpen, kOpen };

    std::vector<Patch> patches;
    std::vector<Node> nodes;
    std::vector<DBL> ranges;
    Cap caps[2];
    bool closed = false, provenBounds = true;
    DBL orientation = 1.0, size = 1.0;
    int gridU = 0, gridV = 0;

    SkeinData() = default;
    SkeinData(const SkeinData&) = delete;
    SkeinData& operator=(const SkeinData&) = delete;
};

/// A surface or solid made by mapping the unit square through a chain of steps.
class Skein final : public ObjectBase
{
    public:

        std::shared_ptr<SkeinData> data;

        Skein();
        virtual ~Skein() override;

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const override;
        virtual void UVCoord(Vector2d&, const Intersection *) const override;
        virtual void Translate(const Vector3d&, const TRANSFORM *) override;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) override;
        virtual void Scale(const Vector3d&, const TRANSFORM *) override;
        virtual void Transform(const TRANSFORM *) override;
        virtual void Compute_BBox() override;

        /// Checks the topology and builds bounds and caps; returns an error message, empty when usable.
        /// `cooperate`, when given, is called now and then and may throw to stop a long Prepare.
        std::string Prepare(TraceThreadData *thread, const std::function<void()>& cooperate = nullptr);

        struct Crossing { DBL t, u, v; Vector3d normal; int sign; };
        static const int kMaxCrossings = 64;

        /// Every crossing of the object-space ray past `tMin`, sorted by depth; `D` must be unit length.
        int FindCrossings(const Vector3d& P, const Vector3d& D, DBL tMin, Crossing *out, TraceThreadData *thread) const;
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_SKEIN_H
