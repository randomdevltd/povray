//******************************************************************************
///
/// @file parser/parser_skein.cpp
///
/// Parsing of the skein geometric primitive.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "parser/parser.h"

// C++ standard header files
#include <cstring>
#include <functional>
#include <map>
#include <memory>

// POV-Ray header files (base module)
#include "base/fileutil.h"
#include "base/types.h"
#include "base/image/image.h"

// POV-Ray header files (core module)
#include "core/math/matrix.h"
#include "core/shape/skein.h"
#include "core/support/imageutil.h"

// POV-Ray header files (VM module)
#include "vm/fnpovfpu.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov_parser
{

using namespace pov;

namespace
{

const FNGroup kSkeinGroups[] = { { "uv", "uv" }, { "pos", "xyz" }, { "norm", "xyz" }, { nullptr, nullptr } };

/// A declared `expressions` group, spliced wherever it is used.
struct SkeinGroup final : public Assignable
{
    std::vector<SkeinStep> steps;
    virtual SkeinGroup *Clone() const override { return new SkeinGroup(*this); }
};

/// The input a function parameter or map word names (`uv.u` or its shorthand `u`), or -1.
int SkeinInput(const std::string& name)
{
    static const char *names[SkeinValue::kInputs][2] = {
        { "u", "uv.u" }, { "v", "uv.v" }, { "x", "pos.x" }, { "y", "pos.y" }, { "z", "pos.z" }, { "nx", "norm.x" }, { "ny", "norm.y" }, { "nz", "norm.z" } };
    for (int i = 0; i < SkeinValue::kInputs; ++i)
        if ((name == names[i][0]) || (name == names[i][1]))
            return i;
    return -1;
}

void AddAffine(std::vector<SkeinStep>& steps, const TRANSFORM& t)
{
    if (!steps.empty() && (steps.back().kind == SkeinStep::kAffine))
        Compose_Transforms(&steps.back().affine, &t);
    else
    {
        steps.push_back(SkeinStep());
        steps.back().affine = t;
    }
}

/// `t` applied about the point `c`.
TRANSFORM About(const TRANSFORM& t, const Vector3d& c)
{
    TRANSFORM a, b;
    Compute_Translation_Transform(&a, -c);
    Compute_Translation_Transform(&b, c);
    Compose_Transforms(&a, &t);
    Compose_Transforms(&a, &b);
    return a;
}

/// Appends a declared group's steps, merging neighbouring affine ones.
void Splice(std::vector<SkeinStep>& steps, const SkeinGroup& group)
{
    for (const SkeinStep& s : group.steps)
        if (s.kind == SkeinStep::kAffine)
            AddAffine(steps, s.affine);
        else
            steps.push_back(s);
}

}
// end of anonymous namespace

// why: skein sub-keywords match by spelling so that no word but `skein` becomes reserved (doc/skein.md#poc-decisions)
ObjectPtr Parser::Parse_Skein()
{
    Parse_Begin();

    Skein *object = reinterpret_cast<Skein *>(Parse_Object_Id());
    if (object != nullptr)
        return reinterpret_cast<ObjectPtr>(object);

    object = new Skein();
    object->data = std::make_shared<SkeinData>();
    SkeinData& data = *object->data;
    bool haveExpressions = false;

    auto parseEnd = [this]() -> SkeinData::End
    {
        Get_Token();
        const UTF8String& word = CurrentTokenText();
        if (word == "flat")
            return SkeinData::kFlat;
        if (word == "pole")
            return SkeinData::kPole;
        if (word == "sealed")
            return SkeinData::kSealed;
        if (word != "open")
            Expectation_Error("flat, pole, sealed or open after skein ends");
        return SkeinData::kOpen;
    };

    for (;;)
    {
        Get_Token();
        const UTF8String word = CurrentTokenText();
        if (word == "expressions")
        {
            if (haveExpressions)
                Error("skein: only one expressions block is allowed.");
            Parse_Skein_Expressions(data.steps);
            haveExpressions = true;
        }
        else if (word == "closed")
        {
            Get_Token();
            const UTF8String& which = CurrentTokenText();
            if ((which == "u") || (which == "uv"))
                data.wrapU = true;
            if ((which == "v") || (which == "uv"))
                data.wrapV = true;
            if (!data.wrapU && !data.wrapV)
                Expectation_Error("u, v or uv after skein closed");
        }
        else if (word == "ends")
        {
            data.ends[0] = data.ends[1] = parseEnd();
            if (Parse_Comma())
                data.ends[1] = parseEnd();
        }
        else
        {
            Unget_Token();
            break;
        }
    }

    if (!haveExpressions)
        Error("skein: an expressions block is required.");
    if ((!data.wrapU || data.wrapV) && ((data.ends[0] != SkeinData::kOpen) || (data.ends[1] != SkeinData::kOpen)))
        Warning("skein: ends apply only when u is closed and v is not; they are ignored here.");

    const std::string problem = object->Prepare(GetParserDataPtr(), [this]() { mProgressReporter.ReportProgress(mTokenCount); });
    if (!problem.empty())
        Error("%s", problem.c_str());
    if (!data.closed)
        object->Type |= PATCH_OBJECT;

    object->Compute_BBox();
    Parse_Object_Mods(reinterpret_cast<ObjectPtr>(object));
    return reinterpret_cast<ObjectPtr>(object);
}

void *Parser::Parse_Skein_Group()
{
    SkeinGroup *group = new SkeinGroup();
    Parse_Skein_Expressions(group->steps);
    return reinterpret_cast<void *>(static_cast<Assignable *>(group));
}

void Parser::Parse_Skein_Expressions(std::vector<SkeinStep>& steps)
{
    Parse_Begin();
    while (!Peek_Token(RIGHT_CURLY_TOKEN))
        Parse_Skein_Step(steps);
    Parse_End();
}

void Parser::Parse_Skein_Step(std::vector<SkeinStep>& steps)
{
    Vector3d vector;
    MATRIX matrix;
    TRANSFORM step;
    Get_Token();
    const TokenId id = CurrentTrueTokenId();
    if (id == EXPRESSIONS_ID_TOKEN)
    {
        Splice(steps, *dynamic_cast<const SkeinGroup *>(CurrentTokenDataPtr<Assignable *>()));
        return;
    }
    if (((id == SCALE_TOKEN) || (id == ROTATE_TOKEN) || (id == TRANSLATE_TOKEN)) && Peek_Token(LEFT_CURLY_TOKEN))
    {
        Parse_Skein_Transform(steps, id);
        return;
    }
    switch (id)
    {
        case SCALE_TOKEN:
            Parse_Scale_Vector(vector);
            Compute_Scaling_Transform(&step, vector);
            break;
        case ROTATE_TOKEN:
            Parse_Vector(vector);
            Compute_Rotation_Transform(&step, vector);
            break;
        case TRANSLATE_TOKEN:
            Parse_Vector(vector);
            Compute_Translation_Transform(&step, vector);
            break;
        case MATRIX_TOKEN:
            Parse_Matrix(matrix);
            Compute_Matrix_Transform(&step, matrix);
            break;
        case TRANSFORM_TOKEN:
            Parse_Transform(&step);
            break;
        default:
            if (CurrentTokenText() == "extrude")
                Parse_Skein_Fold(steps);
            else if (CurrentTokenText() == "crease")
                Parse_Skein_Bend(steps);
            else if (CurrentTokenText() == "bend")
                Parse_Skein_Axial(steps);
            else if (CurrentTokenText() == "curl")
                Parse_Skein_Curl(steps);
            else if (CurrentTokenText() == "fold")
                Parse_Skein_Conform(steps);
            else if (CurrentTokenText() == "displace")
            {
                steps.push_back(SkeinStep());
                steps.back().kind = SkeinStep::kDisplace;
                Parse_Skein_Value(steps.back().value[SkeinStep::kAmount], "displace");
            }
            else if (CurrentTokenText() == "sample")
                Parse_Skein_Sample(steps);
            else if (CurrentTokenText() == "envelope")
                Parse_Skein_Envelope(steps);
            else if (CurrentTokenText() == "expression_map")
                Parse_Skein_Blend(steps);
            else
                Expectation_Error("skein expression: scale, rotate, translate, matrix, transform, extrude, crease, curl, fold, bend, displace, sample, envelope, expression_map or an expressions group");
            return;
    }
    if ((id == SCALE_TOKEN) || (id == ROTATE_TOKEN))
    {
        Get_Token();
        if (CurrentTokenText() == "about")
        {
            Parse_Vector(vector);
            step = About(step, vector);
        }
        else
            Unget_Token();
    }
    AddAffine(steps, step);
}

// why: entries follow Parse_Blend_Map and BlendMap::Search (doc/skein.md, expression_map)
void Parser::Parse_Skein_Blend(std::vector<SkeinStep>& steps)
{
    SkeinStep step;
    step.kind = SkeinStep::kBlend;
    step.blend = std::make_shared<SkeinBlend>();
    std::map<const void *, int> items;
    int count = 0;

    Parse_Begin();
    Get_Token();
    UTF8String word = CurrentTokenText();
    if (CurrentTrueTokenId() == LEFT_SQUARE_TOKEN)
    {
        Unget_Token();
        word = "v";
    }
    else if ((word == "uv") || (word == "pos") || (word == "norm"))
    {
        GET(PERIOD_TOKEN);
        Get_Token();
        word += "." + CurrentTokenText();
    }
    const int input = SkeinInput(word);
    if (input >= 0)
    {
        std::shared_ptr<SkeinMap> map = std::make_shared<SkeinMap>();
        SkeinMap::Node node;
        node.op = SkeinMap::kInput;
        node.input = (unsigned char)input;
        map->nodes.push_back(node);
        map->inputs = 1u << input;
        step.value[SkeinStep::kDriver].kind = SkeinValue::kMap;
        step.value[SkeinStep::kDriver].map = map;
    }
    else
    {
        Unget_Token();
        Parse_Skein_Value(step.value[SkeinStep::kDriver], "expression_map");
    }
    while (Peek_Token(LEFT_SQUARE_TOKEN))
    {
        Parse_Square_Begin();
        SkeinBlend::Entry entry;
        entry.value = Parse_Float();
        Parse_Comma();
        Get_Token();
        if (CurrentTrueTokenId() == EXPRESSIONS_ID_TOKEN)
        {
            const SkeinGroup *group = dynamic_cast<const SkeinGroup *>(CurrentTokenDataPtr<Assignable *>());
            Splice(entry.steps, *group);
            if (items.find(group) == items.end())
                items[group] = count++;
            entry.item = items[group];
        }
        else if (CurrentTokenText() == "expressions")
        {
            Parse_Skein_Expressions(entry.steps);
            entry.item = count++;
        }
        else
        {
            Unget_Token();
            Parse_Skein_Step(entry.steps);
            entry.item = count++;
        }
        Parse_Square_End();
        if (!step.blend->entries.empty() && (entry.value < step.blend->entries.back().value))
            Error("skein expression_map: entries go in increasing order of value; %g follows %g.", entry.value, step.blend->entries.back().value);
        step.blend->entries.push_back(entry);
    }
    Parse_End();
    if (step.blend->entries.empty())
        Error("skein expression_map: at least one entry [value step] is needed.");
    steps.push_back(step);
}

void Parser::Parse_Skein_Transform(std::vector<SkeinStep>& steps, TokenId id)
{
    SkeinStep step;
    step.kind = (id == TRANSLATE_TOKEN) ? SkeinStep::kTranslate : (id == SCALE_TOKEN) ? SkeinStep::kScale : SkeinStep::kRotate;
    step.origin = Vector3d(0.0);
    step.axis = Vector3d(0.0, 1.0, 0.0);
    for (SkeinValue& v : step.value)
        v.constant = (step.kind == SkeinStep::kScale) ? 1.0 : 0.0;
    const char *where[3][3] = { { "translate x", "translate y", "translate z" }, { "scale x", "scale y", "scale z" }, { "rotate angle" } };
    const int row = step.kind - SkeinStep::kTranslate;

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if ((step.kind != SkeinStep::kRotate) && ((word == "x") || (word == "y") || (word == "z")))
            Parse_Skein_Value(step.value[word[0] - 'x'], where[row][word[0] - 'x']);
        else if ((step.kind == SkeinStep::kRotate) && (word == "axis"))
            Parse_Vector(step.axis);
        else if ((step.kind == SkeinStep::kRotate) && (word == "angle"))
            Parse_Skein_Value(step.value[SkeinStep::kAngle], where[row][0]);
        else if ((step.kind != SkeinStep::kTranslate) && (word == "about"))
            Parse_Vector(step.origin);
        else
            Expectation_Error(step.kind == SkeinStep::kRotate ? "rotate parameter: axis, angle or about" :
                              step.kind == SkeinStep::kScale ? "scale parameter: x, y, z or about" : "translate parameter: x, y or z");
    }
    Parse_End();

    if (step.kind == SkeinStep::kRotate)
    {
        if (step.axis.length() < EPSILON)
            Error("skein rotate: axis has zero length.");
        step.axis.normalize();
    }
    if (step.value[0].Varies() || step.value[1].Varies() || step.value[2].Varies())
    {
        steps.push_back(step);
        return;
    }
    TRANSFORM t;
    const Vector3d k(step.value[0].constant, step.value[1].constant, step.value[2].constant);
    if (step.kind == SkeinStep::kTranslate)
        Compute_Translation_Transform(&t, k);
    else if (step.kind == SkeinStep::kScale)
        Compute_Scaling_Transform(&t, k);
    else
        Compute_Axis_Rotation_Transform(&t, step.axis, k[X] * M_PI / 180.0);
    AddAffine(steps, step.kind == SkeinStep::kTranslate ? t : About(t, step.origin));
}

/// An axis of a bend: a direction, a path or sample_path of vectors, or three functions of t; a curved one fills `curve`.
void Parser::Parse_Skein_Axis(Vector3d& axis, std::shared_ptr<SkeinAxis>& curve, const char *where)
{
    const std::string label = std::string(where) + " axis";
    Get_Token();
    if ((CurrentTokenText() == "path") || (CurrentTokenText() == "sample_path"))
    {
        curve = std::make_shared<SkeinAxis>();
        curve->path = Parse_Skein_Path(CurrentTokenText() == "sample_path");
        if (curve->path->dimension != 3)
            Error("skein %s: a path axis needs points that are vectors.", label.c_str());
        if (curve->path->fromSurface)
            curve->source = curve->path;
    }
    else if (CurrentTrueTokenId() == FUNCTION_TOKEN)
    {
        curve = std::make_shared<SkeinAxis>();
        for (int k = 0; k < 3; ++k)
        {
            if ((k > 0) && (!Parse_Comma() || !AllowToken(FUNCTION_TOKEN)))
                Error("skein %s: a curved axis is three functions of t, for x, y and z, separated by commas.", label.c_str());
            SkeinValue value;
            Parse_Skein_Function(value, label.c_str(), true);
            curve->functions[k] = value.function;
        }
    }
    else
    {
        Unget_Token();
        Parse_Vector(axis);
    }
}

void Parser::Parse_Skein_Fold(std::vector<SkeinStep>& steps)
{
    Vector3d axis(0.0, 1.0, 0.0);

    steps.push_back(SkeinStep());
    SkeinStep& step = steps.back();
    step.kind = SkeinStep::kFold;
    step.value[SkeinStep::kRadius].constant = 1.0;
    step.value[SkeinStep::kArc].constant = 360.0;

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if (word == "axis")
            Parse_Vector(axis);
        else if (word == "radius")
            Parse_Skein_Value(step.value[SkeinStep::kRadius], "extrude radius");
        else if (word == "arc")
            Parse_Skein_Value(step.value[SkeinStep::kArc], "extrude arc");
        else
            Expectation_Error("extrude parameter: axis, radius or arc");
    }
    Parse_End();

    if (axis.length() < EPSILON)
        Error("skein extrude: axis has zero length.");
    axis.normalize();
    if ((fabs(axis[Z]) > EPSILON) || ((fabs(axis[X]) > EPSILON) && (fabs(axis[Y]) > EPSILON)))
        Error("skein extrude: an axis is x or y; extrude is a straight line, and a path is bend's along.");

    // the wrapped direction is the one the axis leaves: x for an axis along y, y for an axis along x
    const Vector3d along = fabs(axis[X]) > 0.9 ? Vector3d(0.0, 1.0, 0.0) : Vector3d(1.0, 0.0, 0.0);
    step.axis = axis;
    step.along = along;
    step.side = cross(axis, along);
    step.offset = cross(along, axis);
}

// why: the axis places the frame the transforms turn and shift about; it builds nothing (doc/skein.md, bend)
void Parser::Parse_Skein_Axial(std::vector<SkeinStep>& steps)
{
    Vector3d axis(0.0, 1.0, 0.0), target(0.0, 1.0, 0.0);
    std::shared_ptr<SkeinAxis> curve, path;
    bool haveTransform = false, haveTarget = false;

    steps.push_back(SkeinStep());
    SkeinStep& step = steps.back();
    step.kind = SkeinStep::kAxialStep;
    step.origin = Vector3d(0.0);
    step.value[SkeinStep::kRadial].constant = 1.0;

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        if (CurrentTrueTokenId() == ROTATE_TOKEN)
        {
            Parse_Begin();
            Get_Token();
            if (CurrentTokenText() != "angle")
                Expectation_Error("angle in a bend rotate");
            Parse_Skein_Value(step.value[SkeinStep::kAngle], "bend rotate angle");
            Parse_End();
            haveTransform = true;
        }
        else if (CurrentTrueTokenId() == TRANSLATE_TOKEN)
        {
            Parse_Skein_Value(step.value[SkeinStep::kShift], "bend translate");
            haveTransform = true;
        }
        else if (CurrentTrueTokenId() == SCALE_TOKEN)
        {
            Parse_Skein_Value(step.value[SkeinStep::kRadial], "bend scale");
            haveTransform = true;
        }
        else if (CurrentTokenText() == "along")
        {
            Parse_Skein_Axis(target, path, "bend along");
            haveTransform = haveTarget = true;
        }
        else if (CurrentTokenText() == "axis")
            Parse_Skein_Axis(axis, curve, "bend");
        else
            Expectation_Error("bend parameter: axis, along, rotate { angle }, translate or scale");
    }
    Parse_End();

    if (!haveTransform)
        Error("skein bend: an along, a rotate { angle ... }, a translate or a scale is required.");
    if (curve)
    {
        axis = Vector3d(0.0, 1.0, 0.0);
        step.curve = curve;
    }
    if (axis.length() < EPSILON)
        Error("skein bend: axis has zero length.");
    axis.normalize();
    step.axis = axis;
    if (!haveTarget)
        return;
    // a target is followed in the axis's own frame, so a straight axis becomes a line axis to carry one
    if (!step.curve)
    {
        step.curve = std::make_shared<SkeinAxis>();
        step.curve->line = true;
        step.curve->direction = axis;
    }
    if (!path)
    {
        if (target.length() < EPSILON)
            Error("skein bend: along has zero length.");
        path = std::make_shared<SkeinAxis>();
        path->line = true;
        path->direction = target.normalized();
    }
    step.target = path;
}

void Parser::Parse_Skein_Sample(std::vector<SkeinStep>& steps)
{
    SkeinStep step;
    step.kind = SkeinStep::kSample;
    Parse_Begin();
    Get_Token();
    if (CurrentTokenText() != "at")
        Expectation_Error("sample parameter: at <u value, v value>");
    Parse_Angle_Begin();
    Parse_Skein_Value(step.value[SkeinStep::kAtU], "sample at");
    Parse_Comma();
    Parse_Skein_Value(step.value[SkeinStep::kAtV], "sample at");
    Parse_Angle_End();
    Parse_End();
    steps.push_back(step);
}

void Parser::Parse_Skein_Envelope(std::vector<SkeinStep>& steps)
{
    SkeinStep step;
    step.kind = SkeinStep::kEnvelope;
    bool haveThickness = false;
    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if (word == "thickness")
        {
            Parse_Skein_Value(step.value[SkeinStep::kThickness], "envelope thickness");
            haveThickness = true;
        }
        else if (word == "axis")
        {
            Get_Token();
            if ((CurrentTokenText() != "u") && (CurrentTokenText() != "v"))
                Expectation_Error("u or v after envelope axis");
            step.alongV = CurrentTokenText() == "v";
        }
        else if (word == "edge")
        {
            Get_Token();
            if (CurrentTokenText() == "round")
                step.edge = SkeinStep::kRoundEdge;
            else if (CurrentTokenText() == "flat")
                step.edge = SkeinStep::kFlatEdge;
            else
                Expectation_Error("round or flat after envelope edge");
        }
        else
            Expectation_Error("envelope parameter: thickness, axis or edge");
    }
    Parse_End();
    if (!haveThickness)
        Error("skein envelope: a thickness is required.");
    steps.push_back(step);
}

// why: a straight hinge is exact anywhere in space; a curved one takes bend's station rule (doc/skein.md, crease)
void Parser::Parse_Skein_Bend(std::vector<SkeinStep>& steps)
{
    Vector3d axis(0.0, 1.0, 0.0);
    std::shared_ptr<SkeinAxis> curve;
    bool haveRadius = false, haveAxis = false;
    SkeinStep step;
    step.kind = SkeinStep::kBend;
    step.origin = Vector3d(0.0);
    step.value[SkeinStep::kLimit].constant = 1.0e30;

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if (word == "axis")
        {
            Parse_Skein_Axis(axis, curve, "crease");
            haveAxis = true;
        }
        else if (word == "radius")
        {
            Parse_Skein_Value(step.value[SkeinStep::kRadius], "crease radius");
            haveRadius = true;
        }
        else if (word == "angle")
            Parse_Skein_Value(step.value[SkeinStep::kLimit], "crease angle");
        else
            Expectation_Error("crease parameter: axis, radius or angle");
    }
    Parse_End();

    if (!haveAxis)
        Error("skein crease: an axis is required.");
    if (!haveRadius)
        Error("skein crease: a radius is required.");
    if (!step.value[SkeinStep::kLimit].Varies() && (step.value[SkeinStep::kLimit].constant < 0.0))
        Error("skein crease: angle is a magnitude of 0 or more.");
    Vector3d origin, direction;
    if (curve && curve->path && !curve->path->fromSurface && curve->path->Straight(origin, direction))
    {
        curve.reset();
        step.origin = origin;
        axis = direction;
    }
    if (curve)
    {
        step.curve = curve;
        step.axis = Vector3d(0.0, 1.0, 0.0);
        steps.push_back(step);
        return;
    }
    if (axis.length() < EPSILON)
        Error("skein crease: axis has zero length.");
    step.Hinge(axis);
    steps.push_back(step);
}

// why: the pivot's height is the roll's radius and the travel moves it; the section is tabulated at Prepare (doc/skein.md, curl)
void Parser::Parse_Skein_Curl(std::vector<SkeinStep>& steps)
{
    Vector3d axis(0.0), origin(0.0);
    std::shared_ptr<SkeinAxis> curve;
    std::shared_ptr<SkeinRoll> roll = std::make_shared<SkeinRoll>();
    bool havePivot = false;
    SkeinStep step;
    step.kind = SkeinStep::kCurl;
    step.origin = Vector3d(0.0);

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if (word == "pivot")
        {
            Parse_Skein_Axis(axis, curve, "curl pivot");
            havePivot = true;
        }
        else if (word == "travel")
        {
            Get_Token();
            if (CurrentTokenText() != "path")
                Expectation_Error("path after curl travel");
            std::shared_ptr<SkeinPath> path = Parse_Skein_Path();
            if (path->dimension != 3)
                Error("skein curl: travel is a path of vectors, the pivot's displacement over turns of the roll.");
            roll->travel = path;
        }
        else
            Expectation_Error("curl parameter: pivot or travel");
    }
    Parse_End();

    if (!havePivot)
        Error("skein curl: a pivot is required.");
    if (!roll->travel)
        Error("skein curl: a travel is required.");
    if (!curve)
        Error("skein curl: a pivot given as a direction passes through the origin, so it lies on the sheet; give it as a path or sample_path.");
    if (curve->source)
        roll->source = curve->source;
    else if (!(curve->path && curve->path->Straight(origin, axis)))
        Error("skein curl: the pivot must be a straight line.");
    if (!roll->source)
    {
        if (axis.length() < EPSILON)
            Error("skein curl: pivot has zero length.");
        step.Hinge(axis);
        const DBL radius = dot(origin, step.axis);
        if (fabs(radius) < EPSILON)
            Error("skein curl: a pivot on the sheet has no roll; lift it off the sheet by the roll's radius.");
        step.origin = origin - step.axis * radius;
        step.value[SkeinStep::kRadius].constant = radius;
    }
    step.roll = roll;
    steps.push_back(step);
}

// why: the new normal is three values, as translate takes them, or a perturb in the tangent frame; solved at Prepare (doc/skein.md, fold)
void Parser::Parse_Skein_Conform(std::vector<SkeinStep>& steps)
{
    SkeinStep step;
    step.kind = SkeinStep::kConform;
    std::shared_ptr<SkeinFold> fold = std::make_shared<SkeinFold>();
    bool haveFrom = false, haveNormal = false;
    const char *where[2][3] = { { "fold x", "fold y", "fold z" }, { "fold perturb x", "fold perturb y", "fold perturb z" } };

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        const UTF8String word = CurrentTokenText();
        if (word == "from")
        {
            Parse_UV_Vect(fold->from);
            haveFrom = true;
        }
        else if (((word == "x") || (word == "y") || (word == "z")) && !fold->perturb)
        {
            Parse_Skein_Value(step.value[word[0] - 'x'], where[0][word[0] - 'x']);
            haveNormal = true;
        }
        else if ((word == "perturb") && !haveNormal && !fold->perturb)
        {
            fold->perturb = true;
            step.value[2].constant = 1.0;
            Parse_Begin();
            for (;;)
            {
                Get_Token();
                if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
                {
                    Unget_Token();
                    break;
                }
                const UTF8String part = CurrentTokenText();
                if ((part == "x") || (part == "y") || (part == "z"))
                    Parse_Skein_Value(step.value[part[0] - 'x'], where[1][part[0] - 'x']);
                else
                    Expectation_Error("fold perturb parameter: x, y or z");
            }
            Parse_End();
        }
        else
            Expectation_Error(fold->perturb ? "fold parameter: from (a perturb takes the place of x, y and z)" :
                              haveNormal ? "fold parameter: from, x, y or z (not a perturb as well)" : "fold parameter: from, x, y, z or perturb");
    }
    Parse_End();

    if (!haveFrom)
        Error("skein fold: from <u, v> is required.");
    if (!haveNormal && !fold->perturb)
        Error("skein fold: give the new normal as x, y and z, or as a perturb.");
    if ((fold->from[X] < 0.0) || (fold->from[X] > 1.0) || (fold->from[Y] < 0.0) || (fold->from[Y] > 1.0))
        Error("skein fold: from <%g, %g> is off the sheet; u and v run from 0 to 1.", fold->from[X], fold->from[Y]);
    step.fold = fold;
    steps.push_back(step);
}

// why: two tokens of lookahead tell a value block (`sum {`) from a float expression (`sum(`); doc/skein.md#poc-decisions
bool Parser::Parse_Skein_Followed_By(TokenId next)
{
    const Token_Struct word = mToken;
    Get_Token();
    if (CurrentTrueTokenId() == next)
    {
        Unget_Token();
        return true;
    }
    if (mHavePendingRawToken)
        Error("skein: cannot read past '%s' here; put the expression in parentheses.", word.raw.lexeme.text.c_str());
    UngetRawToken(mToken.raw);
    mToken = word;
    Unget_Token();
    return false;
}

void Parser::Parse_Skein_Value(SkeinValue& value, const char *where)
{
    static const char *blocks[] = { "sum", "product", "min", "max", "map", "path" };
    Get_Token();
    if (CurrentTrueTokenId() == FUNCTION_TOKEN)
    {
        Parse_Skein_Function(value, where, false);
        return;
    }
    if ((CurrentTrueTokenId() == FUNCT_ID_TOKEN) && !Parse_Skein_Next_Is(LEFT_PAREN_TOKEN))
    {
        Get_Token();
        const FUNCTION_PTR declared = CurrentTokenDataPtr<AssignableFunction *>()->fn;
        if (declared == nullptr)
            Error("skein %s: the function is still being declared.", where);
        FUNCTION_PTR fn = mpFunctionVM->CopyFunction(declared);
        value.kind = SkeinValue::kFunction;
        value.function = std::shared_ptr<GenericScalarFunction>(new FunctionVM::CustomFunction(mpFunctionVM.get(), fn));
        Parse_Skein_Inputs(value, mpFunctionVM->GetFunction(*fn), where, true);
        return;
    }
    const UTF8String word = CurrentTokenText();
    int block = -1;
    for (int i = 0; i < 6; ++i)
        if (word == blocks[i])
            block = i;
    if ((block >= 0) && !Parse_Skein_Followed_By(LEFT_CURLY_TOKEN))
        block = -1;
    else if (block < 0)
        Unget_Token();
    if (block == 4)
        Parse_Skein_Map(value);
    else if (block == 5)
    {
        value.kind = SkeinValue::kPath;
        value.path = Parse_Skein_Path();
        if (value.path->dimension != 1)
            Error("skein %s: a path value needs points that are numbers.", where);
    }
    else if (block >= 0)
    {
        value.kind = SkeinValue::kSum;
        value.sum = std::make_shared<SkeinSum>();
        value.sum->op = SkeinSum::Op(block);
        Parse_Begin();
        while (!Peek_Token(RIGHT_CURLY_TOKEN))
        {
            if (Parse_Comma())
                continue;
            value.sum->entries.push_back(SkeinValue());
            Parse_Skein_Value(value.sum->entries.back(), where);
        }
        Parse_End();
        if (value.sum->entries.empty())
            Error("skein %s: %s needs at least one entry.", where, word.c_str());
    }
    else
    {
        value.kind = SkeinValue::kConstant;
        value.constant = Parse_Float();
    }
}

void Parser::Parse_Skein_Function(SkeinValue& value, const char *where, bool axis)
{
    if (!Peek_Token(LEFT_PAREN_TOKEN))
        Error("skein %s: a function needs a parameter list naming its inputs, as in function(uv) { ... }.", where);

    FUNCTION_PTR fn = new FUNCTION;
    FunctionCode code;
    FNCode compiler(this, &code, false, nullptr);
    compiler.GroupedParameter(kSkeinGroups);
    Parse_Begin();
    mFunctionGroups = kSkeinGroups;
    ExprNode *expression = FNSyntax_ParseExpression();
    mFunctionGroups = nullptr;
    compiler.Compile(expression);
    FNSyntax_DeleteExpression(expression);
    Parse_End();
    *fn = mpFunctionVM->AddFunction(&code);
    value.kind = SkeinValue::kFunction;
    value.function = std::shared_ptr<GenericScalarFunction>(new FunctionVM::CustomFunction(mpFunctionVM.get(), fn));

    const FunctionCode *compiled = mpFunctionVM->GetFunction(*fn);
    if (axis)
    {
        if (compiled->parameter_cnt != 1)
            Error("skein %s: a function of one parameter, as in function(t) { ... }.", where);
        return;
    }
    Parse_Skein_Inputs(value, compiled, where, false);
}

void Parser::Parse_Skein_Inputs(SkeinValue& value, const FunctionCode *compiled, const char *where, bool declared)
{
    if (compiled->parameter_cnt > SkeinValue::kInputs)
        Error("skein %s: a function takes at most %d inputs.", where, int(SkeinValue::kInputs));
    unsigned int seen = 0;
    for (unsigned int i = 0; i < compiled->parameter_cnt; ++i)
    {
        const int input = SkeinInput(compiled->parameter[i]);
        if ((input < 0) && declared)
            Error("skein %s: parameter '%s' of a declared function is not a skein input; a declared function names u, v, x, y, z, nx, ny "
                  "or nz (x, y and z by default), and the groups uv, pos and norm need the function written inline.", where, compiled->parameter[i]);
        if (input < 0)
            Error("skein %s: function parameter '%s' is not a skein input; use the groups uv, pos and norm, or u, v, x, y, z, nx, ny and nz.",
                  where, compiled->parameter[i]);
        if (seen & (1u << input))
            Error("skein %s: function input '%s' is listed twice.", where, compiled->parameter[i]);
        seen |= 1u << input;
        value.inputs.push_back((unsigned char)input);
    }
    const unsigned int u = 1u << SkeinValue::kU, v = 1u << SkeinValue::kV, x = 1u << SkeinValue::kX, y = 1u << SkeinValue::kY;
    if (declared && ((((seen & u) != 0) && ((seen & x) != 0)) || (((seen & v) != 0) && ((seen & y) != 0))))
        Error("skein %s: a declared function cannot take both u and x, or v and y, which POV's functions read from one register; write it inline.", where);
}

// why: a declared function names a value only when no ( follows, else it is a call in a float expression; doc/skein.md, batch two
bool Parser::Parse_Skein_Next_Is(TokenId next)
{
    const Token_Struct word = mToken;
    Get_Token();
    const bool is = CurrentTrueTokenId() == next;
    if (mHavePendingRawToken)
        Error("skein: cannot read past '%s' here; put the expression in parentheses.", word.raw.lexeme.text.c_str());
    UngetRawToken(mToken.raw);
    mToken = word;
    Unget_Token();
    return is;
}

void Parser::Parse_Skein_Map(SkeinValue& value)
{
    std::shared_ptr<SkeinMap> map = std::make_shared<SkeinMap>();
    int depth = 0;

    auto block = [this](const std::function<bool(const UTF8String&)>& parameter, const char *expected)
    {
        if (!Peek_Token(LEFT_CURLY_TOKEN))
            return;
        Parse_Begin();
        for (;;)
        {
            Get_Token();
            if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
            {
                Unget_Token();
                break;
            }
            const UTF8String word = CurrentTokenText();
            if (!parameter(word))
                Expectation_Error(expected);
        }
        Parse_End();
    };

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        const TokenId id = CurrentTrueTokenId();
        if (id == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        if (id == COMMA_TOKEN)
            continue;
        UTF8String word = CurrentTokenText();
        if ((word == "uv") || (word == "pos") || (word == "norm"))
        {
            Get_Token();
            if (CurrentTrueTokenId() != PERIOD_TOKEN)
            {
                Unget_Token();
                const int first = word == "uv" ? SkeinValue::kU : word == "pos" ? SkeinValue::kX : SkeinValue::kNX;
                const int count = word == "uv" ? 2 : 3;
                for (int k = 0; k < count; ++k)
                {
                    SkeinMap::Node member;
                    member.op = SkeinMap::kInput;
                    member.input = (unsigned char)(first + k);
                    map->inputs |= 1u << (first + k);
                    map->nodes.push_back(member);
                }
                depth += count;
                if (depth > SkeinMap::kMaxDepth)
                    Error("skein map: more than %d inputs waiting for a modifier.", SkeinMap::kMaxDepth);
                continue;
            }
            Get_Token();
            word += "." + CurrentTokenText();
        }
        SkeinMap::Node node;
        int arity = 1;
        const int input = SkeinInput(word);
        if (input >= 0)
        {
            node.op = SkeinMap::kInput;
            node.input = (unsigned char)input;
            map->inputs |= 1u << input;
            arity = 0;
        }
        else if ((word == "noise") || (word == "fbm"))
        {
            const bool octaves = word == "fbm";
            node.op = octaves ? SkeinMap::kFbm : SkeinMap::kNoise;
            node.a = node.b = 1.0;
            node.c = 2.0;
            node.d = 0.5;
            node.count = octaves ? 4 : 1;
            arity = 3;
            DBL count = node.count;
            block([&](const UTF8String& w)
            {
                if (w == "frequency")
                    node.a = Parse_Float();
                else if (w == "amplitude")
                    node.b = Parse_Float();
                else if (octaves && (w == "octaves"))
                    count = Parse_Float();
                else if (octaves && (w == "lacunarity"))
                    node.c = Parse_Float();
                else if (octaves && (w == "gain"))
                    node.d = Parse_Float();
                else
                    return false;
                return true;
            }, octaves ? "fbm parameter: octaves, lacunarity, gain, frequency or amplitude" : "noise parameter: frequency or amplitude");
            if ((count < 1.0) || (count > 32.0) || (count != floor(count)))
                Error("skein map: fbm takes a whole number of octaves from 1 to 32.");
            node.count = int(count);
        }
        else if (word == "cells")
        {
            node.op = SkeinMap::kCells;
            node.a = node.b = node.c = 1.0;
            arity = 2;
            block([&](const UTF8String& w)
            {
                if (w == "frequency")
                    node.a = Parse_Float();
                else if (w == "amplitude")
                    node.b = Parse_Float();
                else if (w == "form")
                {
                    EXPRESS express;
                    const int terms = Parse_Unknown_Vector(express);
                    if ((terms < 2) || (terms > 3) || ((terms == 3) && (express[Z] != 0.0)))
                        Error("skein map: cells form takes <f1 weight, f2 weight>.");
                    node.c = express[X];
                    node.d = express[Y];
                }
                else
                    return false;
                return true;
            }, "cells parameter: frequency, amplitude or form");
        }
        else if (word == "image")
        {
            node.op = SkeinMap::kImage;
            node.image = Parse_Skein_Image();
            arity = 2;
        }
        else if (word == "linear")
        {
            node.op = SkeinMap::kLinear;
            node.a = 1.0;
            block([&](const UTF8String& w)
            {
                if (w == "scale")
                    node.a = Parse_Float();
                else if (w == "offset")
                    node.b = Parse_Float();
                else
                    return false;
                return true;
            }, "linear parameter: scale or offset");
        }
        else if ((word == "sin") || (word == "cos"))
        {
            node.op = word == "sin" ? SkeinMap::kSin : SkeinMap::kCos;
            node.a = node.b = 1.0;
            block([&](const UTF8String& w)
            {
                if (w == "frequency")
                    node.a = Parse_Float();
                else if (w == "amplitude")
                    node.b = Parse_Float();
                else if (w == "phase")
                    node.c = Parse_Float();
                else
                    return false;
                return true;
            }, "sin or cos parameter: frequency, amplitude or phase");
        }
        else if ((word == "length") || (word == "atan2"))
        {
            node.op = word == "length" ? SkeinMap::kLength : SkeinMap::kAtan2;
            arity = 2;
        }
        else if (word == "range")
        {
            node.op = SkeinMap::kRange;
            node.b = node.d = 1.0;
            block([&](const UTF8String& w)
            {
                if ((w == "from") || (w == "to"))
                {
                    DBL& lo = w == "from" ? node.a : node.c;
                    DBL& hi = w == "from" ? node.b : node.d;
                    lo = Parse_Float();
                    Parse_Comma();
                    hi = Parse_Float();
                }
                else if (w == "repeat")
                    node.method = SkeinMap::kRepeat;
                else if (w == "mirror")
                    node.method = SkeinMap::kMirror;
                else if (w == "clamp")
                    node.method = SkeinMap::kClamp;
                else
                    return false;
                return true;
            }, "range parameter: from, to, repeat, mirror or clamp");
            if (node.a == node.b)
                Error("skein map: range needs an input range of nonzero width.");
        }
        else if (word == "path")
        {
            node.op = SkeinMap::kPath;
            node.path = Parse_Skein_Path();
            if (node.path->dimension != 1)
                Error("skein map: a path modifier needs points that are numbers.");
        }
        else
            Expectation_Error("map input (uv, pos, norm or one member, as uv.u) or modifier (linear, sin, cos, length, atan2, range, path, noise, fbm, cells, image)");
        if (arity > depth)
            Error("skein map: %s takes %d inputs, but %d %s listed before it.", word.c_str(), arity, depth, depth == 1 ? "is" : "are");
        depth += 1 - arity;
        if (depth > SkeinMap::kMaxDepth)
            Error("skein map: more than %d inputs waiting for a modifier.", SkeinMap::kMaxDepth);
        map->nodes.push_back(node);
    }
    Parse_End();
    if (depth != 1)
        Error("skein map: it ends with %d values; a map needs exactly one, so combine inputs with a modifier that takes several (length, atan2, cells, image, noise, fbm).", depth);
    value.kind = SkeinValue::kMap;
    value.map = map;
}

std::shared_ptr<SkeinImage> Parser::Parse_Skein_Image()
{
    Parse_Begin();
    ImageData *image = Parse_Image(IMAGE_FILE);
    int interpolation = BILINEAR;
    for (;;)
    {
        Get_Token();
        if (CurrentTrueTokenId() == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        if (CurrentTokenText() != "interpolate")
            Expectation_Error("image parameter: interpolate");
        interpolation = int(Parse_Float());
        if ((interpolation != BILINEAR) && (interpolation != BICUBIC))
            Error("skein map: an image interpolates 2 (bilinear) or 3 (bicubic); a displacement needs a continuous value.");
    }
    Parse_End();
    std::shared_ptr<SkeinImage> out = std::make_shared<SkeinImage>();
    out->width = image->iwidth;
    out->height = image->iheight;
    out->bicubic = interpolation == BICUBIC;
    out->grey.resize(size_t(out->width) * out->height);
    for (int y = 0; y < out->height; ++y)
        for (int x = 0; x < out->width; ++x)
        {
            RGBFTColour colour;
            image->data->GetRGBFTValue(x, y, colour, image->data->IsPremultiplied());
            out->grey[size_t(y) * out->width + x] = colour.Greyscale();
        }
    Destroy_Image(image);
    out->Build();
    return out;
}

std::shared_ptr<SkeinPath> Parser::Parse_Skein_Path(bool fromSurface)
{
    struct Item { int terms; Vector3d value; bool haveIn, haveOut; Vector3d in, out; };
    std::vector<Item> items;
    std::shared_ptr<SkeinPath> path = std::make_shared<SkeinPath>();
    SkeinPath::Interpolation interpolation = SkeinPath::kLinear;
    bool arclength = false;

    Parse_Begin();
    for (;;)
    {
        Get_Token();
        const TokenId id = CurrentTrueTokenId();
        if (id == RIGHT_CURLY_TOKEN)
        {
            Unget_Token();
            break;
        }
        if (id == COMMA_TOKEN)
            continue;
        const UTF8String word = CurrentTokenText();
        if (id == LINEAR_SPLINE_TOKEN)
            interpolation = SkeinPath::kLinear;
        else if (id == QUADRATIC_SPLINE_TOKEN)
            interpolation = SkeinPath::kQuadratic;
        else if (id == CUBIC_SPLINE_TOKEN)
            interpolation = SkeinPath::kCubic;
        else if (id == NATURAL_SPLINE_TOKEN)
            interpolation = SkeinPath::kNatural;
        else if (word == "closed")
            path->closed = true;
        else if (word == "arclength")
            arclength = true;
        else if ((word == "handle") || (word == "handles"))
        {
            if (items.empty() || (items.back().terms < 2))
                Error("skein path: %s follows the point it belongs to, and that point must be a vector.", word.c_str());
            Item& item = items.back();
            Parse_Vector(item.out);
            item.in = -item.out;
            if (word == "handles")
            {
                item.in = item.out;
                Parse_Comma();
                Parse_Vector(item.out);
            }
            item.haveIn = item.haveOut = true;
        }
        else
        {
            Unget_Token();
            EXPRESS express;
            Item item{ Parse_Unknown_Vector(express), Vector3d(express[X], express[Y], express[Z]), false, false, Vector3d(0.0), Vector3d(0.0) };
            if (item.terms > 3)
                Error("skein path: points are numbers or vectors of up to three components.");
            items.push_back(item);
        }
    }
    Parse_End();

    bool vectors = false;
    for (const Item& item : items)
        vectors = vectors || (item.terms >= 2);
    path->dimension = vectors ? 3 : 1;
    std::vector<SkeinPath::Point> points;
    bool pending = false;
    DBL parameter = 0.0;
    for (const Item& item : items)
    {
        if (vectors && (item.terms == 1))
        {
            if (pending)
                Error("skein path: a parameter value must be followed by its point.");
            pending = true;
            parameter = item.value[X];
            continue;
        }
        SkeinPath::Point point;
        point.point = vectors ? item.value : Vector3d(item.value[X], 0.0, 0.0);
        point.haveParameter = pending;
        point.parameter = parameter;
        point.haveIn = item.haveIn;
        point.haveOut = item.haveOut;
        point.in = item.in;
        point.out = item.out;
        points.push_back(point);
        pending = false;
    }
    if (pending)
        Error("skein path: a parameter value must be followed by its point.");
    const std::string problem = path->Build(points, interpolation);
    if (!problem.empty())
        Error("%s", problem.c_str());
    path->useArc = arclength;
    if (arclength)
    {
        if (!vectors)
            Error("skein path: arclength needs points that are vectors.");
        path->UseArcLength();
    }
    if (fromSurface)
    {
        if (!vectors)
            Error("skein sample_path: points are <u, v, distance along the normal>.");
        path->fromSurface = true;
    }
    return path;
}

}
// end of namespace pov_parser
