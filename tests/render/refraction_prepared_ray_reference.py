#!/usr/bin/env python3
"""Exact Fermat invariant for n(x)=1.1+0.1x, planar entry and exit in air."""
import argparse,json,math
from pathlib import Path
from volume_transport_reference import read_ppm


def ray(x,bend=True):
    if not bend:return {'screen_x':x,'inside_length':1.0,'exit_sine':0.0}
    k=.1;entry=1.1+k*x;a=k/entry
    exit_x=x+entry/k*(math.cosh(a)-1)
    exit_sine=entry*math.sinh(a)
    assert abs(exit_sine)<1
    screen=exit_x+exit_sine/math.sqrt(1-exit_sine**2)
    return {'screen_x':screen,'inside_length':entry/k*math.sinh(a),'exit_sine':exit_sine}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path);a=p.parse_args();images={}
    for name in ['constant','linear','absorbed','native']:
        w,h,values=read_ppm(a.directory/(name+'.ppm'));images[name]=(w,h,values)
    w,h,linear=images['linear'];assert all((iw,ih)==(w,h) for iw,ih,_ in images.values())
    result={};passed=True
    for name in ['constant','linear','absorbed','native']:
        pixels=images[name][2];errors=[]
        for i in range(w*h):
            col=i%w;x0=.1*((col+.5)/w-.5);target=ray(x0,name!='constant')['screen_x']
            red,green=pixels[3*i:3*i+2]
            if green<=0:raise ValueError('Coordinate signal vanished')
            measured=(red/green-.5)/.2
            errors.append(abs(measured-target))
        result[name]={'maximum_screen_coordinate_error':max(errors),'geometry_pass':max(errors)<=.001}
        passed=passed and result[name]['geometry_pass']
    errors=[];positions=[]
    for i in range(w*h):
        x0=.1*((i%w+.5)/w-.5);inside=ray(x0)['inside_length']
        plain=linear[3*i:3*i+3];absorbed=images['absorbed'][2][3*i:3*i+3]
        target=math.exp(-.7*inside);ratio=absorbed[1]/plain[1]
        errors.append(abs(ratio-target)/target)
        positions.append(abs((absorbed[0]/absorbed[1]-plain[0]/plain[1])/.2))
    result['gray_Beer']={'maximum_relative_error':max(errors),'pass':max(errors)<=.03}
    result['sigma_geometry_independence']={'maximum_screen_coordinate_difference':max(positions),'pass':max(positions)<=.001}
    passed=passed and result['gray_Beer']['pass'] and result['sigma_geometry_independence']['pass']
    result['pass']=passed
    (a.directory/'refraction-ray-reference.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    if not passed:raise SystemExit(1)
