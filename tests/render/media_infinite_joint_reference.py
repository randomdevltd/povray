#!/usr/bin/env python3
"""Finite-box single scattering on an infinite camera ray with a rigorous tail bound."""
import argparse
import json
import math
from pathlib import Path
from volume_transport_reference import read_ppm

COLOR = (1, .5, .2)
SCATTER = (.04, .03, .02)
EXTINCTION = (.24, .43, .62)
SIGMA_LOWER = .2
DEPTH = 80.0
NODES = (.1834346424956498, .5255324099163290, .7966664774136267, .9602898564975363)
WEIGHTS = (.3626837833783620, .3137066458778873, .2223810344533745, .1012285362903763)
LUMINANCE = sum(a*b for a,b in zip(COLOR,(.297,.589,.114)))


def source(divisions):
    return [(1+.2*(i+.5)/divisions, -.1+.2*(j+.5)/divisions, .35+.3*(k+.5)/divisions)
        for i in range(divisions) for j in range(divisions) for k in range(divisions)]


def camera_nodes(coarse=False):
    for start,end,width in [(0,4,.5),(4,8,1),(8,16,2),(16,32,4),(32,80,8)]:
        width *= 2 if coarse else 1
        count = int((end-start)/width)
        for i in range(count):
            midpoint,half = start+(i+.5)*width, width/2
            for node,weight in zip(NODES,WEIGHTS):
                yield midpoint-half*node,half*weight
                yield midpoint+half*node,half*weight


def reference(x=0,y=0,divisions=12,coarse=False):
    samples=source(divisions)
    coefficient=100/(16*math.pi*math.pi*LUMINANCE*len(samples))
    total=[0.0]*3
    for t,weight in camera_nodes(coarse):
        z=-1+t
        for sx,sy,sz in samples:
            radius2=(sx-x)**2+(sy-y)**2+(sz-z)**2
            path=t+math.sqrt(radius2)
            for c in range(3):
                total[c]+=weight*math.exp(-EXTINCTION[c]*path)/radius2*SCATTER[c]*COLOR[c]*coefficient
    return total


def tail_bound():
    # r>=.99 for every point on every camera ray in the full frame and every source point.
    intensity=[100*c/(4*math.pi*LUMINANCE) for c in COLOR]
    return [sigma*i/(4*math.pi*.99**2)*math.exp(-SIGMA_LOWER*DEPTH)/SIGMA_LOWER
        for sigma,i in zip(SCATTER,intensity)]


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--pixel-cache',type=Path)
    args=parser.parse_args()
    assert min(EXTINCTION)>=SIGMA_LOWER
    fine=reference();coarse=reference(divisions=6,coarse=True)
    convergence=[abs(a-b)/a for a,b in zip(fine,coarse)]
    if max(convergence)>.005:raise ValueError('Independent camera/source quadrature has not converged')
    result={'camera_limit':DEPTH,'center_rgb':fine,'coarse_center_rgb':coarse,
        'relative_convergence':convergence,'omitted_tail_upper_bound':tail_bound(),
        'tail_assumptions':'Extinction>=.2; source-to-camera-ray perpendicular separation>=.99; source transmission<=1; normalized isotropic phase1/(4pi).'}
    if args.image or args.pixel_cache:
        if args.image:w,h,pixels=read_ppm(args.image)
        else:w,h=8,8
        target=[]
        for row in range(h):
            for col in range(w):target.extend(reference(.02*((col+.5)/w-.5),.02*(.5-(row+.5)/h)))
        if args.pixel_cache:args.pixel_cache.write_text(json.dumps(target)+'\n')
        if args.image:
            channels=[]
            for c in range(3):
                got,expected=pixels[c::3],target[c::3]
                mean,ref=sum(got)/len(got),sum(expected)/len(expected)
                limit=max(2/65535,.03*ref)
                channels.append({'mean':mean,'reference_mean':ref,'mean_error_limit':limit,
                    'relative_bias':(mean-ref)/ref,'relative_pixel_rms':math.sqrt(sum((a-b)**2 for a,b in zip(got,expected))/len(got))/ref,
                    'mean_pass':abs(mean-ref)<=limit})
            result['channels']=channels;result['mean_pass']=all(c['mean_pass'] for c in channels)
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text)
    if args.image and not result['mean_pass']:raise SystemExit(1)
