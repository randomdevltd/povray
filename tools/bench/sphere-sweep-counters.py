#!/usr/bin/env python3
# Diagnostic source generator for the two crossing implementations.
# Class 0: line misses doubled radii; 1: misses original but meets doubled radii;
# 2: accepted crossing; 3: original line intersects but no accepted crossing.
# Classification runs outside the measured intersection interval.
from pathlib import Path
import subprocess
import argparse
parser = argparse.ArgumentParser(description='Emit isolated sphere-sweep counter sources; never edit a checkout in place.')
parser.add_argument('repo', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = args.repo
new=subprocess.check_output(['git','show','8554844ac:source/core/shape/spheresweep.cpp'],cwd=root,text=True)
old=subprocess.check_output(['git','show','232778164:source/core/shape/spheresweep.cpp'],cwd=root,text=True)
base = args.output
base.mkdir(parents=True, exist_ok=True)
base.joinpath('new.cpp').write_text(new)
base.joinpath('old.cpp').write_text(old)
helper=new[new.index('const int kSweepMaxRoots'):new.index('\n}\n// end of anonymous namespace')]
linear=new[new.index('static int IntersectPreciseLinearSweep'):new.index('/*****************************************************************************\n* Local variables')]
profile=r'''
namespace SweepCost {
struct Counts { unsigned long long n=0, instructions=0, ends=0, cuts=0, furthest=0, segments=0; };
thread_local unsigned long long ends=0,cuts=0,furthest=0,segments=0;
struct Meter;
std::mutex registryMutex;
std::vector<Meter*> registry;
void dumpAll();
struct Meter {
 int fd; unsigned long long overhead=0; Counts groups[4];
 Meter() { perf_event_attr a={}; a.size=sizeof(a); a.type=PERF_TYPE_HARDWARE; a.config=PERF_COUNT_HW_INSTRUCTIONS;
  a.exclude_kernel=1; a.exclude_hv=1; a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
  fd=syscall(SYS_perf_event_open,&a,0,-1,-1,0); if(fd<0){perror("sweep perf");std::abort();}
 std::lock_guard<std::mutex> lock(registryMutex); if(registry.empty())std::atexit(dumpAll);registry.push_back(this);}
 unsigned long long get(){unsigned long long v[3];if(read(fd,v,sizeof(v))!=sizeof(v))std::abort(); if(v[1]!=v[2]){fprintf(stderr,"SWEEP multiplexed\n");std::abort();}return v[0];}
 void dump(){for(int i=0;i<4;i++)fprintf(stderr,"SWEEP class=%d calls=%llu instructions=%llu ends=%llu cuts=%llu furthest=%llu segments=%llu\n",i,groups[i].n,groups[i].instructions,groups[i].ends,groups[i].cuts,groups[i].furthest,groups[i].segments);fprintf(stderr,"SWEEP overhead=%llu\n",overhead);close(fd);}
};
void dumpAll(){for(auto* p:registry)p->dump();}
thread_local Meter& meter=*new Meter;
struct Call {
 const SphereSweep& sweep; const Ray& ray; const bool& hit; unsigned long long begin,setup;
 Call(const SphereSweep& s,const Ray& r,const bool& h):sweep(s),ray(r),hit(h){setup=meter.get();ends=cuts=furthest=segments=0; begin=meter.get();}
 ~Call(){auto end=meter.get(); auto ne=ends,nc=cuts,nf=furthest,ns=segments;int group=hit?2:0;
  if(!hit){BasicRay r; if(sweep.Trans){MInvTransRay(r,ray,sweep.Trans);r.Direction.normalize();}else{r.Origin=ray.Origin;r.Direction=ray.Direction;}
   for(int pass=1;pass<=2 && group==0;pass++)for(int i=0;i<sweep.Num_Segments;i++){auto seg=sweep.Segment[i];for(int k=0;k<4;k++)seg.Radius_Coef[k]*=pass;
    if(sweep.Interpolation==LINEAR_SPHERE_SWEEP){SPHSWEEP_INT h[6];if(SweepClass::IntersectPreciseLinearSweep(r,seg,h)){group=pass==1?3:1;break;}}
    else {SweepClass::SweepSpan h[33];if(SweepClass::SweepSegmentSpans(r,seg,i,h)){group=pass==1?3:1;break;}}}}
  auto& g=meter.groups[group];++g.n;g.instructions+=end-begin;g.ends+=ne;g.cuts+=nc;g.furthest+=nf;g.segments+=ns;
  meter.overhead+=begin-setup+meter.get()-end;
 }
};
}
'''
for label,source in [('old',old),('new',new)]:
 s=source
 if label=='new':
  s=s.replace('    double gap[7];\n    const double floor = SweepGap(part, gap);','    ++SweepCost::ends;\n    double gap[7];\n    const double floor = SweepGap(part, gap);',1)
  s=s.replace('    SweepEnds(left, depth + 1, ends, count);','    ++SweepCost::cuts;\n    SweepEnds(left, depth + 1, ends, count);',1)
  s=s.replace('        const SweepPart part = stack[--top];','        ++SweepCost::furthest;\n        const SweepPart part = stack[--top];',1)
 s=s.replace('    Vector3d middle = segment.Center_Coef[0];','    ++SweepCost::segments;\n    Vector3d middle = segment.Center_Coef[0];',1)
 s=s.replace('namespace pov\n{','namespace pov\n{\nnamespace SweepCost { extern thread_local unsigned long long ends,cuts,furthest,segments; }',1)
 start='bool SphereSweep::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)\n{'
 s=s.replace(start,'namespace SweepClass {\n'+helper+'\n'+linear+'\n}\n'+profile+'\n'+start+'\n    bool Intersection_Found = false;\n    SweepCost::Call sweepCost(*this,ray,Intersection_Found);',1)
 s=s.replace('    bool            Intersection_Found = false;','',1)
 s=s.replace('#include <cstdlib>','#include <cstdlib>\n#include <linux/perf_event.h>\n#include <sys/syscall.h>\n#include <unistd.h>\n#include <cstdio>\n#include <mutex>',1)
 base.joinpath(label+'-profile.cpp').write_text(s)
