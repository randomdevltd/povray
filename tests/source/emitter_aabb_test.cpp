#include "core/lighting/emitteraabb.h"
#include <cassert>
#include <iostream>
#include <random>
#include <chrono>

static bool CylinderHit(const pov::Vector3d& p, const pov::Vector3d& w, double radius, double halfLength)
{
    double low=0, high=1e300;
    const double a=w[0]*w[0]+w[1]*w[1], b=p[0]*w[0]+p[1]*w[1];
    const double c=p[0]*p[0]+p[1]*p[1]-radius*radius;
    if (a>0)
    {
        const double discriminant=b*b-a*c;
        if (discriminant<=0) return false;
        const double root=std::sqrt(discriminant);
        low=std::max(low,(-b-root)/a); high=std::min(high,(-b+root)/a);
    }
    else if (c>0) return false;
    if (w[2]!=0)
    {
        double begin=(-halfLength-p[2])/w[2], end=(halfLength-p[2])/w[2];
        if (begin>end) std::swap(begin,end);
        low=std::max(low,begin); high=std::min(high,end);
    }
    else if (std::abs(p[2])>halfLength) return false;
    return high>low;
}

static void CheckTube(double radius, double halfLength, const pov::Vector3d& from)
{
    using pov::Vector3d;
    const int samples=200000;
    const double pi=std::acos(-1.0), bound=std::hypot(std::sqrt(2.0)*radius,halfLength), distance=from.length();
    const double q=(bound/distance)*(bound/distance);
    const double cap=distance>bound?q/(1+std::sqrt(1-q)):2;
    const Vector3d z=-from/distance;
    const Vector3d x=(std::abs(z[0])>.5?Vector3d(-z[2],0,z[0]):Vector3d(0,z[2],-z[1])).normalized();
    const Vector3d y=pov::cross(z,x);
    pov::EmitterAabbProposal proposal(from,Vector3d(-radius,-radius,-halfLength),Vector3d(2*radius,2*radius,2*halfLength));
    double nulls[2];
    for (int box=0;box<2;++box)
    {
        std::mt19937_64 random(81123);
        std::uniform_real_distribution<double> unit(0,1);
        int misses=0;
        const auto start=std::chrono::steady_clock::now();
        for (int i=0;i<samples;++i)
        {
            const double u=unit(random),v=unit(random);
            Vector3d w;
            if (box) w=proposal.Sample(u,v);
            else
            {
                const double co=1-u*cap, si=std::sqrt(std::max(0.0,1-co*co)), phi=2*pi*v;
                w=x*(si*std::cos(phi))+y*(si*std::sin(phi))+z*co;
            }
            misses+=!CylinderHit(from,w,radius,halfLength);
        }
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        nulls[box]=double(misses)/samples;
        std::cout << "tube halfLength=" << halfLength << " proposal=" << box << " null=" << nulls[box] << " seconds=" << seconds << "\n";
    }
    assert(nulls[0]>.95);
    assert(nulls[1]<(halfLength>10?.3:.02));
}

static long double RectangleIntegral(long double d, long double x0, long double x1, long double y0, long double y1)
{
    const int n=8192;
    const long double begin=std::atan2(x0,d), end=std::atan2(x1,d), step=(end-begin)/n;
    long double sum=0;
    for (int i=0;i<=n;++i)
    {
        const long double c=std::cos(begin+i*step), a=d/c;
        const long double value=y1/std::hypot(a,y1)-y0/std::hypot(a,y0);
        sum+=(i==0 || i==n?1:i%2?4:2)*value;
    }
    return sum*step/3;
}

static void CheckInverse(double distance, double width, double height)
{
    using pov::Vector3d;
    for (int axis=0;axis<3;++axis)
    {
        const int a=(axis+1)%3,b=(axis+2)%3;
        Vector3d low(0.0),size(0.0);
        low[axis]=distance; size[axis]=1;
        low[a]=-width*.5; size[a]=width;
        low[b]=-height*.5; size[b]=height;
        pov::EmitterAabbProposal proposal(Vector3d(0.0),low,size);
        assert(proposal.UsesSolidAngle());
        const long double full=RectangleIntegral(distance,-width*.5,width*.5,-height*.5,height*.5);
        for (double u : {.01,.1,.25,.5,.75,.9,.99})
            for (double v : {.1,.5,.9})
            {
                const auto w=proposal.Sample(u,v);
                const long double x=distance*static_cast<long double>(w[a])/w[axis];
                const long double cdf=RectangleIntegral(distance,-width*.5,x,-height*.5,height*.5)/full;
                assert(std::abs(cdf-u)<.0002);
            }
    }
}

int main()
{
    using pov::Vector3d;
    CheckInverse(6000,2,2);
    CheckInverse(100,2e-5,100);
    CheckInverse(1e-7,2,2);
    CheckTube(.024,14.5,Vector3d(4.32,-2.25,0));
    CheckTube(.028,1.2,Vector3d(0,-3.1,0));
    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> unit(0,1);
    for (const Vector3d from : {Vector3d(3,4,5), Vector3d(0,0,-4), Vector3d(0,0,-1.0000001),
                               Vector3d(1e8,2e8,3e8),Vector3d(0,0,0),Vector3d(1,0,0),Vector3d(4.32,-2.25,0),
                               Vector3d(.0240001,-2.25,0),Vector3d(.0240001,-1e-7,0)})
    {
        const bool tube=from[0]==4.32 || from[0]==.0240001;
        const Vector3d size=tube?Vector3d(.048,.048,29):Vector3d(2);
        const Vector3d low=tube?Vector3d(-.024,-.024,-14.5):Vector3d(-1);
        pov::EmitterAabbProposal p(from,low,size);
        double invPdf=0, meanZ=0;
        for (int i=0;i<200000;++i)
        {
            auto w=p.Sample(unit(rng),unit(rng));
            const double pdf=p.Pdf(w);
            assert(std::isfinite(pdf) && pdf>0);
            assert(std::abs(w.length()-1)<1e-12);
            invPdf+=1/pdf; meanZ+=w[2];
        }
        std::cout<<from[0]<<","<<from[1]<<","<<from[2]<<" omega="<<p.SolidAngle()<<" reciprocal="<<invPdf/200000<<" z="<<meanZ/200000<<" angular="<<p.UsesSolidAngle()<<"\n";
        assert(std::abs(invPdf/200000/p.SolidAngle()-1)<.025);
        if (from[0]==3) assert(std::abs(meanZ/200000+.695140196021)<.002);
        if (from[2]==-4) assert(std::abs(meanZ/200000-.966915414298)<.001);
        if (tube) assert(std::abs(meanZ/200000)<.006);
        if (from[2]<-1 && from[2]>-1.001) assert(std::abs(meanZ/200000-.5)<.01);
        for (double u : {0.0,0.5,std::nextafter(1.0,0.0),1.0})
            for (double v : {0.0,0.5,std::nextafter(1.0,0.0),1.0})
            {
                auto w=p.Sample(u,v);
                assert(std::isfinite(w[0]+w[1]+w[2]));
                assert(p.Pdf(w)>0);
            }
    }
    for (const Vector3d from : {Vector3d(0,0,-1e-12),Vector3d(-1e-30,0,-2)})
    {
        pov::EmitterAabbProposal degenerate(from,Vector3d(0,-1,0),Vector3d(1,2,1));
        assert(std::abs(degenerate.SolidAngle()-4*std::acos(-1.0))<1e-14);
        for (double u : {0.0,.5,std::nextafter(1.0,0.0),1.0})
            for (double v : {0.0,.5,std::nextafter(1.0,0.0),1.0})
            {
                const auto w=degenerate.Sample(u,v);
                assert(std::isfinite(w[0]+w[1]+w[2]));
                assert(std::abs(degenerate.Pdf(w)-1/(4*std::acos(-1.0)))<1e-15);
            }
    }
    pov::EmitterAabbProposal unresolved(Vector3d(1e30,2e30,3e30),Vector3d(-1),Vector3d(2));
    assert(std::abs(unresolved.SolidAngle()-4*std::acos(-1.0))<1e-14);
    for (double u : {0.0,.25,.5,.75,1.0})
    {
        const auto w=unresolved.Sample(u,.5);
        assert(std::isfinite(w[0]+w[1]+w[2]));
        assert(std::abs(unresolved.Pdf(w)-1/(4*std::acos(-1.0)))<1e-15);
    }
    pov::EmitterAabbProposal original(Vector3d(3,4,5),Vector3d(-1),Vector3d(2));
    pov::EmitterAabbProposal translated(Vector3d(1e12+3,1e12+4,1e12+5),Vector3d(1e12-1),Vector3d(2));
    assert(original.SolidAngle()==translated.SolidAngle());
    for (int i=0;i<1000;++i)
    {
        double u=unit(rng),v=unit(rng);
        assert((original.Sample(u,v)-translated.Sample(u,v)).length()<1e-15);
    }
}
