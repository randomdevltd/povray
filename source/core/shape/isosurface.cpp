//******************************************************************************
///
/// @file core/shape/isosurface.cpp
///
/// Implementation of the isosurface geometric primitive.
///
/// @author D.Skarda, T.Bily (original code)
/// @author R.Suzuki (modifications)
/// @author Thorsten Froehlich (porting to POV-Ray v3.5)
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
#include "core/shape/isosurface.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

// POV-Ray header files (base module)
#include "base/messenger.h"

// POV-Ray header files (core module)
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/mesh.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;

struct IsosurfaceCache final
{
    const IsoSurface *current;
    Vector3d Pglobal;
    Vector3d Dglobal;
    DBL fmax;
    IsosurfaceCache();
};

IsosurfaceCache::IsosurfaceCache() :
    current(nullptr)
{}

struct ISO_ThreadData final
{
    IsosurfaceCache cache;
    GenericScalarFunctionInstance* pFn;
    DBL Vlength;
    DBL tl;
    DBL shadowFrom;         // any root between these depths will do; shadowTo zero for the first root
    DBL shadowTo;
    int Inv3;
};

/*****************************************************************************
* Local preprocessor defines
******************************************************************************/

struct ISO_Max_Gradient final
{
    DBL max_gradient, gradient;
    DBL eval_max, eval_cnt, eval_gradient_sum, eval_var;
    bool reported;

    ISO_Max_Gradient() :
        max_gradient(0.0),
        gradient(0.0),
        eval_max(0.0),
        eval_cnt(0.0),
        eval_gradient_sum(0.0),
        eval_var(0.0),
        reported(false),
        mRefCounter(0)
    {}

    bool IsShared() const { return mRefCounter > 1; }

private:
    mutable size_t mRefCounter;
    friend void intrusive_ptr_add_ref(ISO_Max_Gradient* f);
    friend void intrusive_ptr_release(ISO_Max_Gradient* f);
};

inline void intrusive_ptr_add_ref(ISO_Max_Gradient* f) { ++f->mRefCounter; }
inline void intrusive_ptr_release(ISO_Max_Gradient* f) { if (!(--f->mRefCounter)) delete f; }


/*****************************************************************************
*
* FUNCTION
*
*   All_IsoSurface_Intersections
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

bool IsoSurface::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    int Side1 = 0, Side2 = 0, itrace = 0;
    DBL Depth1 = 0.0, Depth2 = 0.0;
    BasicRay New_Ray;
    Vector3d IPoint;
    Vector3d Plocal, Dlocal;
    DBL tmax = 0.0, tmin = 0.0, tmp = 0.0;
    DBL maxg = max_gradient;
    int i = 0 ; /* count of intervals in stack - 1      */
    int IFound = false;
    int begin = 0, end = 0;
    bool in_shadow_test = false;
    Vector3d VTmp;
    thread_local ISO_ThreadData isoData;

    Thread->Stats()[Ray_IsoSurface_Bound_Tests]++;

    if(container->Intersect(ray, Trans, Depth1, Depth2, Side1, Side2)) /* IsoSurface_Bound_Tests */
    {
        Thread->Stats()[Ray_IsoSurface_Bound_Tests_Succeeded]++;

        GenericScalarFunctionInstance fn(Function, Thread);

        in_shadow_test = ray.IsShadowTestRay();

        if(Depth1 < 0.0)
            Depth1 = 0.0;

        if (Trans != nullptr)
        {
            MInvTransPoint(Plocal, ray.Origin, Trans);
            MInvTransDirection(Dlocal, ray.Direction, Trans);
        }
        else
        {
            Plocal = ray.Origin;
            Dlocal = ray.Direction;
        }

        isoData.Inv3 = 1;

        if(closed != false)
        {
            VTmp = Plocal + Depth1 * Dlocal;
            tmp = EvaluatePolarized (fn, VTmp);
            if(Depth1 > accuracy)
            {
                if(tmp < 0.0)                   /* The ray hits the bounding shape */
                {
                    IPoint = ray.Evaluate(Depth1);
                    if(Clip.empty() || Point_In_Clip(IPoint, Clip, Thread))
                    {
                        Depth_Stack->push(Intersection(Depth1, IPoint, this, 1, Side1));
                        IFound = true;
                        itrace++;
                        isoData.Inv3 *= -1;
                    }
                }
            }
            else
            {
                if(tmp < (maxg * accuracy * 4.0))
                {
                    Depth1 = accuracy * 5.0;
                    VTmp = Plocal + Depth1 * Dlocal;
                    if (IsInside (fn, VTmp))
                        isoData.Inv3 = -1;
                    /* Change the sign of the function (IPoint is in the bounding shpae.)*/
                }
                VTmp = Plocal + Depth2 * Dlocal;
                if (IsInside (fn, VTmp))
                {
                    IPoint = ray.Evaluate(Depth2);
                    if(Clip.empty() || Point_In_Clip(IPoint, Clip, Thread))
                    {
                        Depth_Stack->push(Intersection(Depth2, IPoint, this, 1, Side2));
                        IFound = true;
                    }
                }
            }
        }

        /*  METHOD 2   by R. Suzuki */
        tmax = Depth2 = min(Depth2, BOUND_HUGE);
        tmin = Depth1 = min(Depth2, Depth1);
        if((tmax - tmin) < accuracy)
        {
            if (IFound)
                Depth_Stack->pop(); // we added an intersection already, so we need to undo that
            return (false);
        }
        Thread->Stats()[Ray_IsoSurface_Tests]++;
        if((Depth1 < accuracy) && (isoData.Inv3 == 1))
        {
            /* IPoint is on the isosurface */
            VTmp = Plocal + tmin * Dlocal;
            if (EvaluateAbs (fn, VTmp) < (maxg * accuracy * 4.0))
            {
                tmin = accuracy * 5.0;
                VTmp = Plocal + tmin * Dlocal;
                if (IsInside (fn, VTmp))
                    isoData.Inv3 = -1;
                /* change the sign and go into the isosurface */
            }
        }

        isoData.pFn = &fn;
        const bool anyHit = Test_Flag(this, OPAQUE_FLAG) && Clip.empty() && !eval && (Thread->isoShadowTo > 0.0);
        isoData.shadowFrom = Thread->isoShadowFrom;
        isoData.shadowTo = anyHit ? Thread->isoShadowTo : 0.0;

        for (; itrace < max_trace; itrace++)
        {
            if(Function_Find_Root(isoData, Plocal, Dlocal, &tmin, &tmax, maxg, in_shadow_test, Thread) == false)
                break;
            else
            {
                IPoint = ray.Evaluate(tmin);
                if(Clip.empty() || Point_In_Clip(IPoint, Clip, Thread))
                {
                    Depth_Stack->push(Intersection(tmin, IPoint, this, 0, 0 /*Side1*/));
                    IFound = true;
                }
                if (anyHit && (tmin > isoData.shadowFrom) && (tmin < isoData.shadowTo))
                    break;
            }
            tmin += accuracy * 5.0;
            if((tmax - tmin) < accuracy)
                break;
            isoData.Inv3 *= -1;
        }

        if(IFound)
            Thread->Stats()[Ray_IsoSurface_Tests_Succeeded]++;

        isoData.pFn = nullptr;
    }

    if(eval == true)
    {
        DBL temp_max_gradient = max_gradient; // TODO FIXME - works around nasty gcc (found using 4.0.1) bug failing to honor casting away of volatile on pass by value on template argument lookup [trf]
        max_gradient = max((DBL)temp_max_gradient, maxg); // TODO FIXME - This is not thread-safe but should be!!! [trf]
    }

    return (IFound);
}


/*****************************************************************************
*
* FUNCTION
*
*   Inside_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

bool IsoSurface::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    Vector3d New_Point;

    /* Transform the point into box space. */
    if (Trans != nullptr)
        MInvTransPoint(New_Point, IPoint, Trans);
    else
        New_Point = IPoint;

    if(!container->Inside(New_Point))
        return (Test_Flag(this, INVERTED_FLAG));

    GenericScalarFunctionInstance fn(Function, Thread);
    if (!IsInside (fn, New_Point))
        return (Test_Flag(this, INVERTED_FLAG));

    /* Inside the box. */
    return (!Test_Flag(this, INVERTED_FLAG));
}


double IsoSurface::GetPotential (const Vector3d& globalPoint, bool subtractThreshold, TraceThreadData *pThread) const
{
    Vector3d localPoint;

    if (Trans != nullptr)
        MInvTransPoint (localPoint, globalPoint, Trans);
    else
        localPoint = globalPoint;

    double potential = GenericScalarFunctionInstance(Function, pThread).Evaluate (localPoint);
    if (subtractThreshold)
        potential -= threshold;

    if (Test_Flag (this, INVERTED_FLAG))
        return -potential;
    else
        return  potential;
}


/*****************************************************************************
*
* FUNCTION
*
*   IsoSurface_Normal
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void IsoSurface::Normal(Vector3d& Result, Intersection *Inter, TraceThreadData *Thread) const
{
    Vector3d New_Point, TPoint;
    DBL funct;
    bool containerHit = Inter->i1;

    if (containerHit)
    {
        container->Normal(Inter->IPoint, Trans, Inter->i2, Result);
    }
    else
    {
        GenericScalarFunctionInstance fn(Function, Thread);

        /* Transform the point into the isosurface space */
        if (Trans != nullptr)
            MInvTransPoint(New_Point, Inter->IPoint, Trans);
        else
            New_Point = Inter->IPoint;

        const DBL xs[4] = { New_Point[X], New_Point[X] + accuracy, New_Point[X], New_Point[X] };
        const DBL ys[4] = { New_Point[Y], New_Point[Y], New_Point[Y] + accuracy, New_Point[Y] };
        const DBL zs[4] = { New_Point[Z], New_Point[Z], New_Point[Z], New_Point[Z] + accuracy };
        DBL f[4];
        fn.Evaluate(xs, ys, zs, f, 4);
        funct = f[0];
        Result[X] = f[1] - funct;
        Result[Y] = f[2] - funct;
        Result[Z] = f[3] - funct;

        if((Result[X] == 0) && (Result[Y] == 0) && (Result[Z] == 0))
            Result[X] = 1.0;
        Result.normalize();

        /* Transform the point into the boxes space. */
        if (Trans != nullptr)
        {
            MTransNormal(Result, Result, Trans);

            Result.normalize();
        }

        if (positivePolarity)
            Result.invert();
    }
}



/*****************************************************************************
*
* FUNCTION
*
*   Translate_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void IsoSurface::Translate(const Vector3d&, const TRANSFORM* tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Rotate_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void IsoSurface::Rotate(const Vector3d&, const TRANSFORM* tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Scale_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void IsoSurface::Scale(const Vector3d&, const TRANSFORM* tr)
{
    Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Transform_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

void IsoSurface::Transform(const TRANSFORM* tr)
{
    if(Trans == nullptr)
        Trans = Create_Transform();

    Compose_Transforms(Trans, tr);

    Compute_BBox();
}



/*****************************************************************************
*
* FUNCTION
*
*   Create_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

IsoSurface::IsoSurface() :
    ObjectBase(ISOSURFACE_OBJECT),
    positivePolarity(false)
{
    container = std::shared_ptr<ContainedByShape>(new ContainedByBox());

    Make_BBox(BBox, -1.0, -1.0, -1.0, 2.0, 2.0, 2.0);

    Trans = Create_Transform();

    Function = nullptr;
    accuracy = 0.001;
    max_trace = 1;

    eval_param[0] = 0.0; // 1.1; // not necessary
    eval_param[1] = 0.0; // 1.4; // not necessary
    eval_param[2] = 0.0; // 0.99; // not necessary
    eval = false;
    closed = true;

    max_gradient = 1.1;
    gradient = 0.0;
    threshold = 0.0;

    mginfo = boost::intrusive_ptr<ISO_Max_Gradient>(new ISO_Max_Gradient());
}



/*****************************************************************************
*
* FUNCTION
*
*   Copy_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

ObjectPtr IsoSurface::Copy()
{
    IsoSurface *New = new IsoSurface();
    Destroy_Transform(New->Trans);
    *New = *this;

    New->Function = Function->Clone();
    New->Trans = Copy_Transform(Trans);

    New->mginfo = mginfo;

    New->positivePolarity = positivePolarity;

    New->container = std::shared_ptr<ContainedByShape>(container->Copy());

    return (New);
}


/*****************************************************************************
*
* FUNCTION
*
*   Destroy_IsoSurface
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

IsoSurface::~IsoSurface()
{
    delete Function;
}

/*****************************************************************************
*
* FUNCTION
*
*   DispatchShutdownMessages
*
* INPUT
*
*   messenger: destination of messages
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Chris Cason
*
* DESCRIPTION
*
*   If any max_gradient messages need to be sent to the user, they are dispatched
*   via the supplied CoreMessenger.
*
* CHANGES
*
******************************************************************************/

void IsoSurface::DispatchShutdownMessages(GenericMessenger& messenger)
{
    // TODO FIXME - works around nasty gcc (found using 4.0.1) bug failing to honor casting
    // away of volatile on pass by value on template argument lookup [trf]
    DBL temp_max_gradient = max_gradient;

    mginfo->gradient = max(gradient, mginfo->gradient);
    mginfo->max_gradient = max((DBL)temp_max_gradient, mginfo->max_gradient);

    if (mginfo->IsShared())
    {
        // Other instances shall take care of reporting the max gradient.
        mginfo.reset();
        return;
    }

    const CustomFunctionSourceInfo* fnInfo = Function->GetSourceInfo();

    if (fnInfo != nullptr)
    {
        if (eval == false)
        {
            // Only show the warning if necessary!
            // BTW, not being too picky here is a feature and not a bug ;-)  [trf]
            if ((mginfo->gradient > EPSILON) && (mginfo->max_gradient > EPSILON))
            {
                DBL diff = mginfo->max_gradient - mginfo->gradient;
                DBL prop = fabs(mginfo->max_gradient / mginfo->gradient);

                if (((prop <= 0.9) && (diff <= -0.5)) || (((prop <= 0.95) || (diff <= -0.1)) && (mginfo->max_gradient < 10.0)))
                {
                    messenger.WarningAt(kWarningGeneral, *fnInfo,
                                        "The maximum gradient found was %0.3f, but max_gradient of the\n"
                                        "isosurface was set to %0.3f. The isosurface may contain holes!\n"
                                        "Adjust max_gradient to get a proper rendering of the isosurface.",
                                        (float)(mginfo->gradient),
                                        (float)(mginfo->max_gradient));
                }
                else if ((diff >= 10.0) || ((prop >= 1.1) && (diff >= 0.5)))
                {
                    messenger.WarningAt(kWarningGeneral, *fnInfo,
                                        "The maximum gradient found was %0.3f, but max_gradient of\n"
                                        "the isosurface was set to %0.3f. Adjust max_gradient to\n"
                                        "get a faster rendering of the isosurface.",
                                        (float)(mginfo->gradient),
                                        (float)(mginfo->max_gradient));
                }
            }
        }
        else
        {
            DBL diff = (mginfo->eval_max / max(mginfo->eval_max - mginfo->eval_var, EPSILON));

            if ((eval_param[0] > mginfo->eval_max) || (eval_param[1] > diff))
            {
                mginfo->eval_cnt = max(mginfo->eval_cnt, 1.0); // make sure it won't be zero

                messenger.InfoAt(*fnInfo,
                                    "Evaluate found a maximum gradient of %0.3f and an average\n"
                                    "gradient of %0.3f. The maximum gradient variation was %0.3f.\n",
                                    (float)(mginfo->eval_max),
                                    (float)(mginfo->eval_gradient_sum / mginfo->eval_cnt),
                                    (float)(mginfo->eval_var));
            }
        }
    }
}

/*****************************************************************************
*
* FUNCTION
*
*   Compute_IsoSurface_BBox
*
* INPUT
*
*   ISOSURFACE - IsoSurface
*
* OUTPUT
*
*   ISOSURFACE
*
* RETURNS
*
* AUTHOR
*
* DESCRIPTION
*
*   Calculate the bounding box of an Isosurface.
*
* CHANGES
*
******************************************************************************/

void IsoSurface::Compute_BBox()
{
    container->ComputeBBox(BBox);
    if (Trans != nullptr)
    {
        Recompute_BBox(&BBox, Trans);
    }
}


/*****************************************************************************
*
* FUNCTION
*
*   IsoSurface_Function_Find_Root
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

bool IsoSurface::Function_Find_Root(ISO_ThreadData& itd, const Vector3d& PP, const Vector3d& DD, DBL* Depth1, DBL* Depth2, DBL& maxg, bool in_shadow_test, TraceThreadData* pThreadData)
{
    DBL dt, t21, l_b, l_e, oldmg;
    ISO_Pair EP1, EP2;
    Vector3d VTmp;

    pThreadData->Stats()[Ray_IsoSurface_Find_Root]++;

    itd.Vlength = DD.length();

    if(itd.cache.current == this)
    {
        pThreadData->Stats()[Ray_IsoSurface_Cache]++;
        VTmp = PP + *Depth1 * DD;
        VTmp -= itd.cache.Pglobal;
        l_b = VTmp.length();
        VTmp = PP + *Depth2 * DD;
        VTmp -= itd.cache.Dglobal;
        l_e = VTmp.length();
        if((itd.cache.fmax - maxg * max(l_b, l_e)) > 0.0)
        {
            pThreadData->Stats()[Ray_IsoSurface_Cache_Succeeded]++;
            return false;
        }
    }

    itd.cache.Pglobal = PP;
    itd.cache.Dglobal = DD;

    itd.cache.current = nullptr;
    DBL p1, p2;
    Polarized_Pair(itd, *Depth1, *Depth2, p1, p2);
    EP1.t = *Depth1;
    EP1.f = (DBL)itd.Inv3 * p1;
    itd.cache.fmax = EP1.f;
    if((closed == false) && (EP1.f < 0.0))
    {
        itd.Inv3 *= -1;
        EP1.f *= -1;
    }

    EP2.t = *Depth2;
    EP2.f = (DBL)itd.Inv3 * p2;
    itd.cache.fmax = min(EP2.f, itd.cache.fmax);

    oldmg = maxg;
    t21 = (*Depth2 - *Depth1);
    if((eval == true) && (oldmg > eval_param[0]))
        maxg = oldmg * eval_param[2];
    dt = maxg * itd.Vlength * t21;
    if(Function_Find_Root_R(itd, &EP1, &EP2, dt, t21, 1.0 / (itd.Vlength * t21), maxg, pThreadData))
    {
        if(eval == true)
        {
            DBL curvar = fabs(maxg - oldmg);

            if(curvar > mginfo->eval_var)
                mginfo->eval_var = curvar;

            mginfo->eval_cnt++;
            mginfo->eval_gradient_sum += maxg;

            if(maxg > mginfo->eval_max)
                mginfo->eval_max = maxg;
        }

        *Depth1 = itd.tl;

        return true;
    }
    else if(!in_shadow_test)
    {
        itd.cache.Pglobal = PP + EP1.t * DD;
        itd.cache.Dglobal = PP + EP2.t * DD;
        itd.cache.current = this;

        return false;
    }

    return false;
}

/*****************************************************************************
*
* FUNCTION
*
*   IsoSurface_Function_Find_Root_R
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

bool IsoSurface::Function_Find_Root_R(ISO_ThreadData& itd, const ISO_Pair* EP1, const ISO_Pair* EP2, DBL dt, DBL t21, DBL len, DBL& maxg, TraceThreadData* pThreadData)
{
    ISO_Pair EPa;
    DBL temp;

    temp = fabs((EP2->f - EP1->f) * len);
    if(gradient < temp)
        gradient = temp;

    if((eval == true) && (maxg < temp * eval_param[1]))
    {
        maxg = temp * eval_param[1] * eval_param[1];
        dt = maxg * itd.Vlength * t21;
    }

    if(t21 < accuracy)
    {
        if(EP2->f < 0)
        {
            itd.tl = EP2->t;
            return true;
        }
        else
            return false;
    }

    if((EP1->f + EP2->f - dt) < 0)
    {
        t21 *= 0.5;
        dt *= 0.5;
        EPa.t = EP1->t + t21;
        EPa.f = Float_Function(itd, EPa.t);

        itd.cache.fmax = min(EPa.f, itd.cache.fmax);
        // Either half's search depends only on its ends, so a shadow ray may try the half ending inside first.
        if ((itd.shadowTo > 0.0) && (EP2->f < 0) && (EP1->t > itd.shadowFrom) && (EP2->t < itd.shadowTo))
            return Function_Find_Root_R(itd, &EPa, EP2, dt, t21, len * 2.0, maxg, pThreadData) ||
                   Function_Find_Root_R(itd, EP1, &EPa, dt, t21, len * 2.0, maxg, pThreadData);
        if(!Function_Find_Root_R(itd, EP1, &EPa, dt, t21, len * 2.0, maxg, pThreadData))
            return (Function_Find_Root_R(itd, &EPa, EP2, dt, t21, len * 2.0,maxg, pThreadData));
        else
            return true;
    }
    else
        return false;
}


/*****************************************************************************
*
* FUNCTION
*
*   Float_IsoSurface_Function
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   R. Suzuki
*
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

DBL IsoSurface::Float_Function(ISO_ThreadData& itd, DBL t) const
{
    Vector3d VTmp;

    VTmp = itd.cache.Pglobal + t * itd.cache.Dglobal;

    return ((DBL)itd.Inv3 * EvaluatePolarized (*itd.pFn, VTmp));
}


void IsoSurface::Polarized_Pair(ISO_ThreadData& itd, DBL t1, DBL t2, DBL& p1, DBL& p2) const
{
    const Vector3d a = itd.cache.Pglobal + t1 * itd.cache.Dglobal;
    const Vector3d b = itd.cache.Pglobal + t2 * itd.cache.Dglobal;
    const DBL xs[2] = { a.x(), b.x() };
    const DBL ys[2] = { a.y(), b.y() };
    const DBL zs[2] = { a.z(), b.z() };
    DBL f[2];
    itd.pFn->Evaluate(xs, ys, zs, f, 2);
    p1 = positivePolarity ? (threshold - f[0]) : (f[0] - threshold);
    p2 = positivePolarity ? (threshold - f[1]) : (f[1] - threshold);
}

/*****************************************************************************/

DBL IsoSurface::EvaluateAbs (GenericScalarFunctionInstance& fn, Vector3d& p) const
{
    return fabs (threshold - fn.Evaluate (p));
}

DBL IsoSurface::EvaluatePolarized (GenericScalarFunctionInstance& fn, Vector3d& p) const
{
    if (positivePolarity)
        return threshold - fn.Evaluate (p);
    else
        return fn.Evaluate (p) - threshold;
}

bool IsoSurface::IsInside (GenericScalarFunctionInstance& fn, Vector3d& p) const
{
    if (positivePolarity)
        return threshold < fn.Evaluate (p);
    else
        return fn.Evaluate (p) < threshold;
}

namespace
{

const int kMeshDepth = 16;
const int kMeshRoots = 8;
const int kMeshProgress = 4096;
const DBL kMeshDefaultSize = 1.0e-3;
const DBL kMeshOffset = 0.3172;
const DBL kMeshStep = 1.0e-3;
const DBL kMeshNudge = 0.1;
const DBL kMeshSpread = 1.5;
const DBL kMeshCrease = 3.0;
const DBL kMeshTruncate = 0.1;
const DBL kMeshReach = 0.5;
const DBL kMeshGap = 2.0;
const DBL kMeshSheet = 150.0 * M_PI / 180.0;

DBL Angle(const Vector3d& a, const Vector3d& b)
{
    const DBL l = a.length() * b.length();
    return l > 0.0 ? acos(std::min(1.0, std::max(-1.0, dot(a, b) / l))) : 0.0;
}

DBL Spread(const Vector3d *n) { return std::max(Angle(n[0], n[1]), std::max(Angle(n[1], n[2]), Angle(n[2], n[0]))); }

Vector3d Unit(const Vector3d& v) { const DBL l = v.length(); return l > 0.0 ? v / l : v; }

template<typename M> size_t MapBytes(const M& m)
{
    return m.size() * (sizeof(typename M::value_type) + 2 * sizeof(void *)) + m.bucket_count() * sizeof(void *);
}

/// Eigenvalues `w` and eigenvectors (the columns of `v`) of a symmetric 3x3 matrix, by Jacobi rotations.
void Eigen(DBL a[3][3], DBL w[3], DBL v[3][3])
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            v[i][j] = (i == j) ? 1.0 : 0.0;
    for (int sweep = 0; (sweep < 32) && (fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]) > 1.0e-300); ++sweep)
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q)
            {
                if (a[p][q] == 0.0)
                    continue;
                const DBL theta = 0.5 * (a[q][q] - a[p][p]) / a[p][q];
                const DBL t = (theta >= 0.0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                const DBL c = 1.0 / sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 3; ++k)
                {
                    const DBL kp = a[k][p], kq = a[k][q];
                    a[k][p] = c * kp - s * kq;
                    a[k][q] = s * kp + c * kq;
                }
                for (int k = 0; k < 3; ++k)
                {
                    const DBL pk = a[p][k], qk = a[q][k];
                    a[p][k] = c * pk - s * qk;
                    a[q][k] = s * pk + c * qk;
                }
                for (int k = 0; k < 3; ++k)
                {
                    const DBL kp = v[k][p], kq = v[k][q];
                    v[k][p] = c * kp - s * kq;
                    v[k][q] = s * kp + c * kq;
                }
            }
    for (int i = 0; i < 3; ++i)
        w[i] = a[i][i];
}

/// The planes through the crossings around one cell, from which dual contouring places the cell's vertex.
struct Qef final
{
    DBL ata[3][3] = {}, atb[3] = {}, mass[3] = {};
    std::uint64_t key = 0;
    int count = 0, size = 0;

    void Add(const Vector3d& p, const Vector3d& n)
    {
        const DBL d = dot(n, p);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                ata[i][j] += n[i] * n[j];
            atb[i] += n[i] * d;
            mass[i] += p[i];
        }
        ++count;
    }

    Vector3d Mass() const { return Vector3d(mass[0], mass[1], mass[2]) / DBL(count); }

    /// The point nearest every plane, moved off the mass point only along directions the planes pin down.
    Vector3d Solve() const
    {
        const Vector3d m = Mass();
        DBL a[3][3], w[3], v[3][3];
        Vector3d r;
        for (int i = 0; i < 3; ++i)
        {
            r[i] = atb[i];
            for (int j = 0; j < 3; ++j)
            {
                a[i][j] = ata[i][j];
                r[i] -= ata[i][j] * m[j];
            }
        }
        Eigen(a, w, v);
        const DBL top = std::max(fabs(w[0]), std::max(fabs(w[1]), fabs(w[2])));
        Vector3d x = m;
        for (int k = 0; k < 3; ++k)
            if (fabs(w[k]) > kMeshTruncate * top)
            {
                const Vector3d e(v[0][k], v[1][k], v[2][k]);
                x += e * (dot(e, r) / w[k]);
            }
        return x;
    }
};

/// Dual contouring on an octree over `contained_by`, splitting cells that may hold surface while the gradient turns too much across them;
/// each edge with a sign change joins the vertices of the cells around it, so cells of different sizes share their faces.
class IsoMesher final
{
    public:

        IsoMesher(const IsoSurface& s, TraceThreadData *t, DBL size, DBL angle, MeshBuilder& m, IsoSurfaceMeshReport& r, const std::function<void()>& p) :
            iso(s), fn(s.Function, t), box(dynamic_cast<const ContainedByBox *>(s.container.get())),
            ball(dynamic_cast<const ContainedBySphere *>(s.container.get())), unit(size), maxAngle(angle * M_PI / 180.0),
            slope(s.max_gradient), out(m), report(r), progress(p)
        {}

        std::string Run()
        {
            if (!box && !ball)
                return "isosurface_mesh: contained_by must be a box or a sphere.";
            const Vector3d lo = box ? box->corner1 : ball->center - Vector3d(ball->radius);
            const Vector3d hi = box ? box->corner2 : ball->center + Vector3d(ball->radius);
            const Vector3d extent = hi - lo;
            const DBL longest = std::max(extent[X], std::max(extent[Y], extent[Z]));
            if (!(longest > 0.0))
                return "isosurface_mesh: contained_by has no volume.";
            unit = std::max(unit < 0.0 ? kMeshDefaultSize * longest : unit, longest / (DBL(kMeshRoots) * DBL(1 << kMeshDepth)));
            while ((depth < kMeshDepth) && (unit * DBL(2 << depth) * kMeshRoots <= longest))
                ++depth;
            step = kMeshStep * unit;
            const DBL root = unit * DBL(1 << depth);
            for (int a = 0; a < 3; ++a)
            {
                const int n = std::max(1, int(ceil((extent[a] + 3.0 * unit) / root)));
                cells[a] = n << depth;
                origin[a] = 0.5 * (lo[a] + hi[a] - n * root) + kMeshOffset * unit;
            }
            for (int i = 0; i < cells[X]; i += Size(0))
                for (int j = 0; j < cells[Y]; j += Size(0))
                    for (int k = 0; k < cells[Z]; k += Size(0))
                        stack.push_back(Cell{ { i, j, k }, 0 });

            size_t maybe = 0, visited = 0;
            while (!stack.empty())
            {
                const Cell c = stack.back();
                stack.pop_back();
                if ((++visited % kMeshProgress) == 0)
                {
                    progress();
                    Peak();
                }
                const int size = Size(c.level);
                const Kind kind = Classify(c.at, size);
                if ((kind == kMaybe) && Split(c.at, size))
                {
                    const int half = size / 2;
                    for (int k = 0; k < 8; ++k)
                        stack.push_back(Cell{ { c.at[0] + ((k & 1) ? half : 0), c.at[1] + ((k & 2) ? half : 0), c.at[2] + ((k & 4) ? half : 0) }, c.level + 1 });
                    continue;
                }
                if (kind != kProved)
                    leaves[Key(c.at)] = Leaf{ -1, std::uint8_t(c.level), kind };
                if ((kind == kMaybe) && (++maybe > 2 * IsoSurface::kMaxMeshTriangles))
                    return TooMany();
            }
            std::vector<Cell>().swap(stack);
            Peak();

            const std::string problem = Contour();
            if (!problem.empty())
                return problem;
            Peak();
            decltype(leaves)().swap(leaves);
            decltype(samples)().swap(samples);
            if (quads.empty())
                return "isosurface_mesh: no surface inside contained_by.";

            Place();
            Peak();
            std::vector<Qef>().swap(qefs);
            for (size_t k = 0; k < quads.size(); ++k)
            {
                if (((k + 1) % kMeshProgress) == 0)
                    progress();
                const Quad& q = quads[k];
                if (q.cap && !iso.closed)
                    continue;
                std::int32_t v[4];
                int n = 0;
                for (int i = 0; i < 4; ++i)
                    if (std::find(v, v + n, q.v[i]) == v + n)
                        v[n++] = q.v[i];
                if (n == 3)
                    Emit(v[0], v[1], v[2]);
                else if (n == 4)
                {
                    const Vector3d m[2] = { 0.5 * (where[v[0]] + where[v[2]]), 0.5 * (where[v[1]] + where[v[3]]) };
                    DBL g[2];
                    Field(m, 2, g);
                    if (fabs(g[0]) <= fabs(g[1]))
                    {
                        Emit(v[0], v[1], v[2]);
                        Emit(v[0], v[2], v[3]);
                    }
                    else
                    {
                        Emit(v[0], v[1], v[3]);
                        Emit(v[1], v[2], v[3]);
                    }
                }
            }
            Peak();
            if (out.Triangles() == 0)
                return "isosurface_mesh: no surface inside contained_by.";
            std::vector<Quad>().swap(quads);
            std::vector<Vector3d>().swap(where);
            std::vector<Vector3d>().swap(normal);
            decltype(ids)().swap(ids);
            std::sort(edges.begin(), edges.end());
            report.openEdges = 0;
            for (size_t k = 0, n; k < edges.size(); k += n)
            {
                for (n = 1; (k + n < edges.size()) && (edges[k + n] == edges[k]); ++n) {}
                report.openEdges += (n == 1);
            }
            std::vector<std::uint64_t>().swap(edges);
            out.Finish(iso.closed || (report.openEdges == 0));
            return std::string();
        }

    private:

        enum Kind : std::uint8_t { kProved, kSampled, kMaybe };
        struct Leaf { std::int32_t slot; std::uint8_t level; Kind kind; };
        struct Sample { DBL g; float n[3]; bool normal; };
        struct Quad { std::int32_t v[4]; bool cap; };
        struct Cell { int at[3]; int level; };
        struct VertexKey
        {
            std::int32_t slot;
            std::uint32_t n[3];
            bool operator==(const VertexKey& o) const { return (slot == o.slot) && (n[0] == o.n[0]) && (n[1] == o.n[1]) && (n[2] == o.n[2]); }
        };
        struct VertexHash
        {
            size_t operator()(const VertexKey& k) const
            {
                return size_t(std::uint64_t(k.slot) * 0x9E3779B97F4A7C15ull ^ (std::uint64_t(k.n[0]) << 1) ^ (std::uint64_t(k.n[1]) << 21) ^ (std::uint64_t(k.n[2]) << 42));
            }
        };

        static std::uint64_t Key(const int *at) { return (std::uint64_t(at[0]) << 42) | (std::uint64_t(at[1]) << 21) | std::uint64_t(at[2]); }
        static void Unkey(std::uint64_t key, int *at) { at[0] = int(key >> 42); at[1] = int((key >> 21) & 0x1FFFFF); at[2] = int(key & 0x1FFFFF); }
        int Size(int level) const { return 1 << (depth - level); }
        Vector3d Point(const int *at) const { return origin + Vector3d(at[0], at[1], at[2]) * unit; }

        std::string TooMany() const
        {
            char text[160];
            snprintf(text, sizeof(text), "isosurface_mesh: more than %g million triangles at this min_size and max_angle; raise either (min_size is %g).",
                     1.0e-6 * IsoSurface::kMaxMeshTriangles, unit);
            return text;
        }

        void Peak()
        {
            const size_t bytes = MapBytes(leaves) + MapBytes(samples) + MapBytes(ids) + stack.capacity() * sizeof(Cell) + qefs.capacity() * sizeof(Qef) +
                                 quads.capacity() * sizeof(Quad) + 2 * where.capacity() * sizeof(Vector3d) + edges.capacity() * sizeof(std::uint64_t) + out.Bytes(true);
            report.peakBytes = std::max(report.peakBytes, bytes);
        }

        DBL Container(const Vector3d& p) const
        {
            if (ball)
                return (p - ball->center).length() - ball->radius;
            DBL c = -BOUND_HUGE;
            for (int a = 0; a < 3; ++a)
                c = std::max(c, std::max(box->corner1[a] - p[a], p[a] - box->corner2[a]));
            return c;
        }

        void ContainerRange(const Vector3d& lo, const Vector3d& hi, DBL& clo, DBL& chi) const
        {
            if (ball)
            {
                Vector3d near, far;
                for (int a = 0; a < 3; ++a)
                {
                    near[a] = std::min(std::max(ball->center[a], lo[a]), hi[a]) - ball->center[a];
                    far[a] = std::max(fabs(lo[a] - ball->center[a]), fabs(hi[a] - ball->center[a]));
                }
                clo = near.length() - ball->radius;
                chi = far.length() - ball->radius;
                return;
            }
            clo = chi = -BOUND_HUGE;
            for (int a = 0; a < 3; ++a)
            {
                const DBL c1 = box->corner1[a], c2 = box->corner2[a], mid = std::min(std::max(0.5 * (c1 + c2), lo[a]), hi[a]);
                clo = std::max(clo, std::max(c1 - mid, mid - c2));
                chi = std::max(chi, std::max(c1 - lo[a], hi[a] - c2));
            }
        }

        /// The field meshed: the isosurface's function, inside where negative, cut by its container; `f` gets the function's part alone.
        void Field(const Vector3d *p, int n, DBL *g, DBL *f = nullptr)
        {
            DBL x[7], y[7], z[7], v[7];
            for (int i = 0; i < n; ++i)
            {
                x[i] = p[i][X];
                y[i] = p[i][Y];
                z[i] = p[i][Z];
            }
            fn.Evaluate(x, y, z, v, n);
            for (int i = 0; i < n; ++i)
            {
                const DBL fp = iso.positivePolarity ? iso.threshold - v[i] : v[i] - iso.threshold;
                if (f)
                    f[i] = fp;
                g[i] = std::max(fp, Container(p[i]));
            }
        }

        Vector3d Gradient(const Vector3d& p)
        {
            Vector3d q[6];
            for (int a = 0; a < 3; ++a)
            {
                q[2 * a] = q[2 * a + 1] = p;
                q[2 * a][a] += step;
                q[2 * a + 1][a] -= step;
            }
            DBL g[6];
            Field(q, 6, g);
            return Vector3d(g[0] - g[1], g[2] - g[3], g[4] - g[5]) / (2.0 * step);
        }

        Sample& Corner(const int *at, bool withNormal)
        {
            const std::uint64_t key = Key(at);
            auto found = samples.find(key);
            Sample *s = (found != samples.end()) ? &found->second : nullptr;
            const Vector3d p = Point(at);
            if (!s)
            {
                s = &samples[key];
                s->normal = false;
                Field(&p, 1, &s->g);
            }
            if (withNormal && !s->normal)
            {
                Vector3d q[3] = { p, p, p };
                DBL g[3];
                for (int a = 0; a < 3; ++a)
                    q[a][a] += step;
                Field(q, 3, g);
                for (int a = 0; a < 3; ++a)
                    s->n[a] = float((g[a] - s->g) / step);
                s->normal = true;
            }
            return *s;
        }

        Kind Classify(const int *at, int size)
        {
            const int far[3] = { at[0] + size, at[1] + size, at[2] + size };
            const Vector3d lo = Point(at), hi = Point(far);
            DBL clo, chi, flo, fhi;
            ContainerRange(lo, hi, clo, chi);
            if (clo > 0.0)
                return kProved;
            const bool ranged = iso.Function->EvaluateRange(lo, hi, flo, fhi);
            if (ranged)
            {
                ++report.rangeCells;
                if (iso.positivePolarity)
                {
                    const DBL t = iso.threshold - fhi;
                    fhi = iso.threshold - flo;
                    flo = t;
                }
                else
                {
                    flo -= iso.threshold;
                    fhi -= iso.threshold;
                }
            }
            else
            {
                ++report.sampledCells;
                const Vector3d centre = 0.5 * (lo + hi);
                DBL g, f;
                Field(&centre, 1, &g, &f);
                const DBL r = slope * 0.5 * (hi - lo).length();
                flo = f - r;
                fhi = f + r;
            }
            if ((std::max(flo, clo) > 0.0) || (std::max(fhi, chi) < 0.0))
                return ranged ? kProved : kSampled;
            return kMaybe;
        }

        /// Whether the gradient at the cell's corners and centre turns more than the limit, unless it falls into two or three tight
        /// groups far apart, a crease or corner that the cell's vertex can sit on; an open rim is always split.
        bool Split(const int *at, int size)
        {
            if (size == 1)
                return false;
            Vector3d n[9], seeds[4];
            for (int k = 0; k < 8; ++k)
            {
                const int c[3] = { at[0] + ((k & 1) ? size : 0), at[1] + ((k & 2) ? size : 0), at[2] + ((k & 4) ? size : 0) };
                const Sample& s = Corner(c, true);
                n[k] = Vector3d(s.n[0], s.n[1], s.n[2]);
            }
            n[8] = Gradient(Point(at) + Vector3d(0.5 * size * unit));
            DBL turn = 0.0;
            for (int a = 0; a < 9; ++a)
                for (int b = a + 1; b < 9; ++b)
                    turn = std::max(turn, Angle(n[a], n[b]));
            if (turn <= maxAngle)
                return false;
            const int far[3] = { at[0] + size, at[1] + size, at[2] + size };
            DBL clo, chi;
            ContainerRange(Point(at), Point(far), clo, chi);
            if (!iso.closed && (clo <= 0.0) && (chi >= 0.0))
                return true;
            int groups = 0;
            for (int k = 0; k < 9; ++k)
            {
                int g = 0;
                while ((g < groups) && (Angle(n[k], seeds[g]) > 0.5 * maxAngle))
                    ++g;
                if (g < groups)
                    continue;
                if (groups == 3)
                    return true;
                seeds[groups++] = n[k];
            }
            for (int a = 0; a < groups; ++a)
                for (int b = a + 1; b < groups; ++b)
                {
                    const DBL turn = Angle(seeds[a], seeds[b]);
                    if ((turn < kMeshGap * maxAngle) || (turn > kMeshSheet))
                        return true;
                }
            return false;
        }

        /// The leaf holding unit cell `u` if it is at least `size` wide, else null: that part is split finer, or lies outside.
        Leaf *Find(const int *u, int size, std::uint64_t& key)
        {
            for (int a = 0; a < 3; ++a)
                if ((u[a] < 0) || (u[a] >= cells[a]))
                    return nullptr;
            for (int s = size; s <= Size(0); s *= 2)
            {
                const int o[3] = { u[0] & ~(s - 1), u[1] & ~(s - 1), u[2] & ~(s - 1) };
                auto found = leaves.find(Key(o));
                if (found == leaves.end())
                    continue;
                if (Size(found->second.level) < s)
                    return nullptr;
                key = found->first;
                return &found->second;
            }
            return nullptr;
        }

        Vector3d Root(Vector3d a, DBL ga, Vector3d b, DBL gb)
        {
            const DBL tolerance = 1.0e-6 * unit;
            Vector3d m = a;
            int side = 0;
            for (int n = 0; n < 64; ++n)
            {
                m = a + (b - a) * (ga / (ga - gb));
                DBL gm;
                Field(&m, 1, &gm);
                if (gm == 0.0)
                    break;
                if ((gm < 0.0) == (ga < 0.0))
                {
                    a = m;
                    ga = gm;
                    if (side == -1)
                        gb *= 0.5;
                    side = -1;
                }
                else
                {
                    b = m;
                    gb = gm;
                    if (side == 1)
                        ga *= 0.5;
                    side = 1;
                }
                if ((b - a).length() < tolerance)
                    break;
            }
            return m;
        }

        /// Finds each edge with a sign change that no smaller cell splits, once, and joins the cells around it; a cell ruled out that
        /// still touches one shows max_gradient was too low.
        std::string Contour()
        {
            size_t triangles = 0, seen = 0;
            for (auto& entry : leaves)
            {
                if ((++seen % kMeshProgress) == 0)
                    progress();
                if (entry.second.kind != kMaybe)
                    continue;
                int at[3];
                Unkey(entry.first, at);
                const int size = Size(entry.second.level);
                for (int a = 0; a < 3; ++a)
                    for (int e = 0; e < 4; ++e)
                    {
                        const int b = (a + 1) % 3, c = (a + 2) % 3;
                        int p0[3] = { at[0], at[1], at[2] };
                        p0[b] += (e & 1) ? size : 0;
                        p0[c] += (e & 2) ? size : 0;
                        int p1[3] = { p0[0], p0[1], p0[2] };
                        p1[a] += size;
                        const DBL g0 = Corner(p0, false).g, g1 = Corner(p1, false).g;
                        if ((g0 < 0.0) == (g1 < 0.0))
                            continue;
                        const int mine = (e & 1) ? ((e & 2) ? 0 : 3) : ((e & 2) ? 1 : 2);
                        Leaf *around[4];
                        std::uint64_t keys[4];
                        int owner = -1;
                        bool whole = true;
                        for (int q = 0; whole && (q < 4); ++q)
                        {
                            int u[3] = { p0[0], p0[1], p0[2] };
                            u[b] -= ((q == 0) || (q == 3)) ? 1 : 0;
                            u[c] -= (q < 2) ? 1 : 0;
                            around[q] = Find(u, size, keys[q]);
                            whole = (around[q] != nullptr);
                            if (whole && (owner < 0) && (around[q]->kind == kMaybe) && (Size(around[q]->level) == size))
                                owner = q;
                        }
                        if (!whole || (owner != mine))
                            continue;
                        const Vector3d x = Root(Point(p0), g0, Point(p1), g1);
                        DBL gx, fx;
                        Field(&x, 1, &gx, &fx);
                        const bool cap = Container(x) > fx;
                        const Vector3d n = Unit(Gradient(x));
                        std::int32_t v[4];
                        int distinct = 0;
                        for (int q = 0; q < 4; ++q)
                        {
                            Leaf& leaf = *around[q];
                            if (leaf.slot < 0)
                            {
                                report.missedCells += (leaf.kind != kMaybe);
                                leaf.slot = std::int32_t(qefs.size());
                                qefs.emplace_back();
                                qefs.back().key = keys[q];
                                qefs.back().size = Size(leaf.level);
                            }
                            v[q] = leaf.slot;
                            if (std::find(v, v + q, v[q]) == v + q)
                            {
                                qefs[v[q]].Add(x, n);
                                ++distinct;
                            }
                        }
                        triangles += distinct - 2;
                        if (triangles > IsoSurface::kMaxMeshTriangles)
                            return TooMany();
                        quads.push_back(g0 < 0.0 ? Quad{ { v[0], v[1], v[2], v[3] }, cap } : Quad{ { v[3], v[2], v[1], v[0] }, cap });
                    }
            }
            return std::string();
        }

        static bool Within(const Vector3d& x, const Vector3d& lo, const Vector3d& hi, DBL slack)
        {
            for (int a = 0; a < 3; ++a)
            {
                const DBL margin = slack * (hi[a] - lo[a]);
                if ((x[a] < lo[a] - margin) || (x[a] > hi[a] + margin))
                    return false;
            }
            return true;
        }

        /// Places each cell's vertex where its planes meet (the mass point if that is outside the cell), then moves it onto the surface.
        void Place()
        {
            where.resize(qefs.size());
            normal.resize(qefs.size());
            for (size_t k = 0; k < qefs.size(); ++k)
            {
                if (((k + 1) % kMeshProgress) == 0)
                    progress();
                const Qef& q = qefs[k];
                int at[3];
                Unkey(q.key, at);
                const Vector3d lo = Point(at), hi = lo + Vector3d(q.size * unit);
                Vector3d x = q.Solve();
                if (!Within(x, lo, hi, kMeshReach))
                    x = q.Mass();
                DBL g;
                Field(&x, 1, &g);
                for (int n = 0; (n < 4) && (g != 0.0); ++n)
                {
                    const Vector3d d = Gradient(x);
                    if (d.lengthSqr() == 0.0)
                        break;
                    const Vector3d y = x - d * (g / d.lengthSqr());
                    DBL gy;
                    Field(&y, 1, &gy);
                    if (!Within(y, lo, hi, kMeshReach) || (fabs(gy) >= fabs(g)))
                        break;
                    x = y;
                    g = gy;
                }
                where[k] = x;
                normal[k] = Unit(Gradient(x));
            }
        }

        MeshIndex Vertex(std::int32_t slot, const Vector3d& n)
        {
            const SnglVector3d s(n);
            const SNGL bits[3] = { s[X], s[Y], s[Z] };
            VertexKey key{ slot, { 0, 0, 0 } };
            std::memcpy(key.n, bits, sizeof(key.n));
            auto found = ids.find(key);
            if (found != ids.end())
                return found->second;
            return ids[key] = out.Vertex(where[slot], n);
        }

        /// Adds a triangle; where its corner normals spread wider than a smooth cell's, a corner on a crease takes the gradient just inside
        /// the triangle, and a triangle still spanning a crease takes its face's normal.
        void Emit(std::int32_t a, std::int32_t b, std::int32_t c)
        {
            const std::int32_t s[3] = { a, b, c };
            const Vector3d raw = cross(where[b] - where[a], where[c] - where[a]);
            if (raw.lengthSqr() == 0.0)
                return;
            Vector3d n[3] = { normal[a], normal[b], normal[c] };
            if (Spread(n) > kMeshSpread * maxAngle)
            {
                const Vector3d centre = (where[a] + where[b] + where[c]) / 3.0;
                for (int i = 0; i < 3; ++i)
                {
                    const Vector3d inside = Unit(Gradient(where[s[i]] + kMeshNudge * (centre - where[s[i]])));
                    if (Angle(inside, n[i]) > maxAngle)
                        n[i] = inside;
                }
                if (Spread(n) > kMeshCrease * maxAngle)
                {
                    const Vector3d face = Unit(raw) * (dot(raw, n[0] + n[1] + n[2]) < 0.0 ? -1.0 : 1.0);
                    n[0] = n[1] = n[2] = face;
                }
            }
            if (!out.Triangle(Vertex(a, n[0]), Vertex(b, n[1]), Vertex(c, n[2])))
                return;
            for (int e = 0; e < 3; ++e)
            {
                const std::int32_t p = std::min(s[e], s[(e + 1) % 3]), q = std::max(s[e], s[(e + 1) % 3]);
                edges.push_back((std::uint64_t(p) << 32) | std::uint64_t(q));
            }
        }

        const IsoSurface& iso;
        GenericScalarFunctionInstance fn;
        const ContainedByBox *box;
        const ContainedBySphere *ball;
        DBL unit, maxAngle, slope, step = 0.0;
        MeshBuilder& out;
        IsoSurfaceMeshReport& report;
        const std::function<void()>& progress;
        Vector3d origin;
        int depth = 0, cells[3] = { 0, 0, 0 };
        std::vector<Cell> stack;
        std::unordered_map<std::uint64_t, Leaf> leaves;
        std::unordered_map<std::uint64_t, Sample> samples;
        std::vector<Qef> qefs;
        std::vector<Quad> quads;
        std::vector<Vector3d> where, normal;
        std::unordered_map<VertexKey, MeshIndex, VertexHash> ids;
        std::vector<std::uint64_t> edges;
};

}
// end of anonymous namespace

std::string IsoSurface::Tessellate(TraceThreadData *thread, DBL minSize, DBL maxAngle, MeshBuilder& mesh, IsoSurfaceMeshReport& report,
                                   const std::function<void()>& progress) const
{
    IsoMesher mesher(*this, thread, minSize, maxAngle, mesh, report, progress);
    return mesher.Run();
}

}
// end of namespace pov
