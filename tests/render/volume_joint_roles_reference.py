#!/usr/bin/env python3
"""Independent piecewise optical fields for constant-base modifier roles."""
import argparse
import json
import math
from pathlib import Path
import volume_joint_media_reference as integral
from volume_transport_reference import read_ppm


def regions(case):
    if case in (1, 2):
        return [(0, 0.25, 1), (0.25, 0.75, 0.5 if case == 1 else 0.75), (0.75, 1, 1)]
    return [(0, 1, 1)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=int, choices=range(4), default=0)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--control', type=Path)
    args = parser.parse_args()
    if args.case == 3:
        if not args.image or not args.control:
            raise ValueError('Independent index field requires matched explicit-density control with identical index boundaries')
        w,h,pixels=read_ppm(args.image)
        cw,ch,target=read_ppm(args.control)
        if (w,h)!=(cw,ch):raise ValueError('Control dimensions differ')
        channels=[]
        for c in range(3):
            mean=sum(pixels[c::3])/(w*h);ref=sum(target[c::3])/(w*h)
            channels.append({'mean':mean,'control_mean':ref,'relative_bias':(mean-ref)/ref,
                'mean_pass':abs(mean-ref)<=max(2/65535,.03*ref)})
        result={'case':3,'comparison':'Matched explicit-density control, same nonzero refraction; no absolute straight-slab oracle claim',
            'channels':channels,'mean_pass':all(c['mean_pass'] for c in channels)}
        text=json.dumps(result,indent=2)+'\n'
        if args.output:args.output.write_text(text)
        print(text)
        raise SystemExit(0 if result['mean_pass'] else 1)
    integral.regions = regions
    fine = integral.reference(case=args.case)
    coarse = integral.reference(case=args.case, source_divisions=6, depth_divisions=32)
    convergence = max(abs(a-b)/a for a,b in zip(fine,coarse))
    if convergence > 0.005:
        raise ValueError('Independent voxel quadrature has not converged')
    result = {'case': args.case, 'center_rgb': fine, 'relative_convergence': convergence}
    if args.image:
        w,h,pixels = read_ppm(args.image)
        target = []
        for row in range(h):
            for col in range(w):
                target.extend(integral.reference(.02*((col+.5)/w-.5), .02*(.5-(row+.5)/h), args.case))
        channels=[]
        for c in range(3):
            measured, expected = pixels[c::3], target[c::3]
            mean, ref = sum(measured)/(w*h), sum(expected)/(w*h)
            channels.append({'mean':mean, 'reference_mean':ref, 'relative_bias':(mean-ref)/ref,
                'relative_pixel_rms':math.sqrt(sum((a-b)**2 for a,b in zip(measured,expected))/(w*h))/ref,
                'mean_pass':abs(mean-ref)<=max(2/65535,.03*ref)})
        result['channels']=channels
        result['mean_pass']=all(c['mean_pass'] for c in channels)
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text)
    if args.image and not result['mean_pass']:raise SystemExit(1)
