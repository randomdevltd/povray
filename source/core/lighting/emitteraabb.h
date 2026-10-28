#ifndef POVRAY_CORE_EMITTERAABB_H
#define POVRAY_CORE_EMITTERAABB_H

#include "core/configcore.h"
#include "core/math/vector.h"
#include <algorithm>
#include <cmath>

namespace pov
{

class EmitterAabbProposal final
{
    using Real = long double;
    struct Face
    {
        int axis, a, b;
        Real offset, d, x0, x1, y0, y1, area, omega, b0, b1, k;
        bool angular;
    };
    Face faces[3];
    int count = 0;
    Real total = 0;
    bool angular = true;
    static Real Pi() { return std::acos(Real(-1)); }
    static Real Clamp(Real x) { return std::max(Real(-1), std::min(Real(1), x)); }
    static Real Length(Real x, Real y, Real z) { return std::hypot(std::hypot(x,y),z); }
    static Real Triangle(Real x0, Real y0, Real x1, Real y1, Real x2, Real y2, Real d)
    {
        const Real l0=Length(x0,y0,d), l1=Length(x1,y1,d), l2=Length(x2,y2,d);
        const Real det=d*(x1-x0)*(y2-y0)/(l0*l1*l2);
        const Real den=1+(x0*x1+y0*y1+d*d)/(l0*l1)+
                            (x1*x2+y1*y2+d*d)/(l1*l2)+(x2*x0+y2*y0+d*d)/(l2*l0);
        return 2*std::atan2(det,den);
    }
    public:
    EmitterAabbProposal(const Vector3d& from, const Vector3d& low, const Vector3d& size)
    {
        Real lo[3], hi[3];
        bool inside=true, validAngles=true;
        for (int i=0;i<3;++i)
        {
            lo[i]=Real(low[i])-Real(from[i]); hi[i]=lo[i]+Real(size[i]);
            inside=inside && lo[i]<=0 && hi[i]>=0;
        }
        if (inside) { total=4*Pi(); return; }
        for (int i=0;i<3;++i)
        {
            if (lo[i]<=0 && hi[i]>=0) continue;
            Face& f=faces[count++]; f.axis=i; f.a=(i+1)%3; f.b=(i+2)%3;
            f.offset=lo[i]>0?lo[i]:hi[i]; f.d=std::abs(f.offset);
            f.x0=lo[f.a]; f.x1=hi[f.a]; f.y0=lo[f.b]; f.y1=hi[f.b];
            f.area=Real(size[f.a])*Real(size[f.b]);
            f.omega=Triangle(f.x0,f.y0,f.x1,f.y0,f.x1,f.y1,f.d)+
                    Triangle(f.x0,f.y0,f.x1,f.y1,f.x0,f.y1,f.d);
            const Real hx0=std::hypot(f.d,f.x0);
            const Real hy0=std::hypot(f.d,f.y0), hy1=std::hypot(f.d,f.y1);
            f.b0=-f.y0/hy0; f.b1=f.y1/hy1;
            const Real g2=std::acos(Clamp(f.x0*f.y1/(hx0*hy1)));
            const Real g3=std::acos(Clamp(-f.x0*f.y0/(hx0*hy0)));
            f.k=2*Pi()-g2-g3;
            total+=f.omega;
            f.angular=f.omega>1e-7L && std::isfinite(f.omega);
            angular=angular && f.angular;
            validAngles=validAngles && f.omega>0 && std::isfinite(f.omega);
            if (f.angular && (std::abs(f.b0)>=1 || std::abs(f.b1)>=1)) validAngles=false;
        }
        for (int i=0;i<count;++i)
            if (total-faces[i].omega==total) validAngles=false;
        if (!validAngles)
        {
            count=0;
            total=4*Pi();
            angular=true;
        }
    }
    double SolidAngle() const { return double(total); }
    bool UsesSolidAngle() const { return angular; }
    Vector3d Sample(double u, double v) const
    {
        if (!count)
        {
            const double z=1-2*u, r=std::sqrt(std::max(0.0,1-z*z)), p=double(2*Pi())*v;
            return Vector3d(r*std::cos(p),r*std::sin(p),z);
        }
        Real pick=Real(u)*total;
        int i=0;
        for (;i+1<count;++i)
        {
            const Real mass=faces[i].omega;
            if (pick<mass) break;
            pick-=mass;
        }
        const Face& f=faces[i];
        Real x,y;
        if (f.angular)
        {
            // Urena et al. (2013), spherical rectangle inverse: https://doi.org/10.1111/cgf.12151
            const Real au=pick+f.k;
            const Real fu=(std::cos(au)*f.b0-f.b1)/std::sin(au);
            const Real cu=std::copysign(1/std::hypot(fu,f.b0),fu);
            x=std::max(f.x0,std::min(f.x1,cu*f.d/std::sqrt(std::max(Real(0),1-cu*cu))));
            const Real d=std::hypot(x,f.d);
            const Real h0=f.y0/std::hypot(d,f.y0), h1=f.y1/std::hypot(d,f.y1);
            const Real h=h0+Real(v)*(h1-h0);
            y=std::max(f.y0,std::min(f.y1,h*d/std::sqrt(std::max(Real(0),(1-h)*(1+h)))));
        }
        else { x=f.x0+(f.x1-f.x0)*pick/f.omega; y=f.y0+(f.y1-f.y0)*Real(v); }
        const Real r=Length(x,y,f.offset);
        Vector3d w; w[f.axis]=double(f.offset/r); w[f.a]=double(x/r); w[f.b]=double(y/r);
        return w;
    }
    double Pdf(const Vector3d& w) const
    {
        if (!count) return double(1/(4*Pi()));
        for (int i=0;i<count;++i)
        {
            const Face& f=faces[i];
            if (w[f.axis]==0) continue;
            const Real t=f.offset/Real(w[f.axis]);
            if (!(t>0)) continue;
            const Real x=t*Real(w[f.a]), y=t*Real(w[f.b]);
            const Real tol=64*Real(2.2204460492503131e-16)*std::max({f.d,std::abs(x),std::abs(y)});
            if (x<f.x0-tol || x>f.x1+tol || y<f.y0-tol || y>f.y1+tol) continue;
            if (f.angular) return double(1/total);
            return double((f.omega/total)*t*t/(f.area*std::abs(Real(w[f.axis]))));
        }
        return 0;
    }
};

}
#endif
