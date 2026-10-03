// skein_shapes.inc: skein test shapes, each beside the existing primitive it must equal.
#declare SkeinTorus = skein {
  expressions {
    extrude { radius 0.5 }
    extrude { axis x  radius 2 }
  }
  closed uv
}
#declare PrimTorus = torus { 2, 0.5 rotate z*90 }

#declare SkeinTube = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
  }
  closed u
  ends flat
}
#declare PrimTube = cylinder { <0, 0, 0>, <0, 2, 0>, 0.5 }

#declare SkeinFrustum = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius function(v) { 1 - 0.75*v } }
  }
  closed u
  ends flat
}
#declare PrimFrustum = cone { <0, 0, 0>, 1, <0, 2, 0>, 0.25 }

#declare SkeinTwistedTube = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
    rotate { axis y  angle function(v) { 90*v } }
  }
  closed u
  ends flat
}

#declare Hexagon = function(u) { 0.5*cos(pi/6) / cos(mod(2*pi*u, pi/3) - pi/6) }
#declare SkeinHexColumn = skein {
  expressions {
    scale <1, 3, 1>
    extrude { radius function(u) { Hexagon(u) } }
  }
  closed u
  ends flat
}
#declare PrimHexColumn = prism {
  linear_sweep linear_spline 0, 3, 7,
  <0.5, 0>, <0.25, -0.5*sin(pi/3)>, <-0.25, -0.5*sin(pi/3)>, <-0.5, 0>, <-0.25, 0.5*sin(pi/3)>, <0.25, 0.5*sin(pi/3)>, <0.5, 0>
}

#declare SkeinTwistedColumn = skein {
  expressions {
    scale <1, 3, 1>
    extrude { radius function(u, v) { Hexagon(u) * (1 - 0.4*v) } }
    rotate { axis y  angle function(v) { 60*v } }
  }
  closed u
  ends flat
}

// Grouped inputs: the frustum's radius read from uv, from pos, and from v and y together, which the VM's registers once aliased.
#declare SkeinFrustumUV = skein {
  expressions { scale <1, 2, 1>  extrude { radius function(uv) { 1 - 0.75*uv.v } } }
  closed u  ends flat
}
#declare SkeinFrustumPos = skein {
  expressions { scale <1, 2, 1>  extrude { radius function(pos) { 1 - 0.375*pos.y } } }
  closed u  ends flat
}
#declare SkeinFrustumMixed = skein {
  expressions { scale <1, 2, 1>  extrude { radius function(v, y) { 1 - 1.5*v + 0.375*y } } }
  closed u  ends flat
}
#declare SkeinTubeNormal = skein {
  expressions { scale <1, 2, 1>  extrude { radius function(norm) { 0.5*norm.z } } }
  closed u  ends flat
}

// Displace: a constant thickens the torus; a function of the normal and the point bulges the tube.
#declare SkeinTorusDisplaced = skein {
  expressions {
    extrude { radius 0.5 }
    extrude { axis x  radius 2 }
    displace 0.1
  }
  closed uv
}
#declare PrimTorusDisplaced = torus { 2, 0.6 rotate z*90 }
#declare SkeinTubeDisplaced = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
    displace function(norm, pos) { 0.1 + 0.05*norm.x + 0.02*pos.y }
  }
  closed u  ends flat
}

// Map: the frustum from a linear map, from a reversed range, and from a scale map after the extrude; a length map thickens the tube.
#declare SkeinFrustumLinear = skein {
  expressions { scale <1, 2, 1>  extrude { radius map { uv.v  linear { scale -0.75  offset 1 } } } }
  closed u  ends flat
}
#declare SkeinFrustumRange = skein {
  expressions { scale <1, 2, 1>  extrude { radius map { uv.v  range { to 1, 0.25 } } } }
  closed u  ends flat
}
#declare SkeinFrustumScaled = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 1 }
    scale { x map { pos.y  linear { scale -0.375  offset 1 } }  z map { pos.y  linear { scale -0.375  offset 1 } } }
  }
  closed u  ends flat
}
#declare SkeinTubeLength = skein {
  expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace map { pos.x, pos.z  length  linear { scale 0.2 } } }
  closed u  ends flat
}
#declare PrimTubeThick = cylinder { <0, 0, 0>, <0, 2, 0>, 0.6 }

// Map against implicit surfaces: a sway, ridges from atan2, lobes from a repeated u and a bicone from a mirrored v.
#declare SkeinTubeSway = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
    translate {
      x map { uv.v  sin { amplitude 0.2 } }
      z map { uv.v  cos { amplitude 0.1 } }
    }
  }
  closed u  ends flat
}
#declare SkeinTubeRidged = skein {
  expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace map { pos.z, pos.x  atan2  sin { frequency 3/(2*pi)  amplitude 0.05 } } }
  closed u  ends flat
}
#declare SkeinTubeLobes = skein {
  expressions { scale <1, 2, 1>  extrude { radius map { uv.u  linear { scale 3 }  range { repeat }  sin { amplitude 0.05 }  linear { offset 0.5 } } } }
  closed u  ends flat
}
#declare SkeinBicone = skein {
  expressions { scale <1, 2, 1>  extrude { radius map { uv.v  linear { scale 2 }  range { mirror }  linear { scale 0.3  offset 0.5 } } } }
  closed u  ends flat
}

// Path: the frustum from a scalar path, a straight tube bent along a path with handles, and the frustum along an arclength path.
#declare SkeinFrustumPath = skein {
  expressions { scale <1, 2, 1>  extrude { radius path { 1, 0.25 } } }
  closed u  ends flat
}
#declare SkeinTubeHandles = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius 0.5 }
    bend { axis y  along path { <0, 0, 0> handle <0, 2/3, 0>  <0, 2, 0> handle <0, 2/3, 0> } }
  }
  closed u  ends flat
}
#declare SkeinFrustumArc = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius map { uv.v  linear { scale -0.75  offset 1 } } }
    bend { axis y  along path { <0, 0, 0>, <0, 0.2, 0>, <0, 2, 0>  arclength } }
  }
  closed u  ends flat
}

// A vase from a natural spline radius (the previewer's `cubic` profile), and the (2, 3) torus knot as a tube bent along a curve.
#declare SkeinVase = skein {
  expressions { scale <1, 2.2, 1>  extrude { radius path { natural_spline 0.35, 0.55, 0.6, 0.42, 0.22, 0.26, 0.38 } } }
  closed u  ends flat, open
}
#declare KnotAngle = function(s) { 2*pi*s }
#declare KnotRadius = function(s) { 2 + cos(3*KnotAngle(s)) }
// a bend keeps the material's length, so the tube is as long as the knot: its speed is 2 pi sqrt(9 + 4 K squared)
#declare KnotSpeed = function(s) { 2*pi*sqrt(9 + 4*pow(KnotRadius(s), 2)) }
#declare KnotLength = 0;
#declare KnotPanel = 0;
#while (KnotPanel < 32)
  #declare KnotLength = KnotLength +
    (KnotSpeed(KnotPanel/32) + 4*KnotSpeed((KnotPanel + 0.5)/32) + KnotSpeed((KnotPanel + 1)/32))/192;
  #declare KnotPanel = KnotPanel + 1;
#end
#declare SkeinTrefoil = skein {
  expressions {
    scale <1, KnotLength, 1>
    extrude { radius 0.05 }
    bend {
      axis y
      along function(t) { KnotRadius(t)*cos(2*KnotAngle(t)) }, function(t) { KnotRadius(t)*sin(2*KnotAngle(t)) }, function(t) { -sin(3*KnotAngle(t)) }
    }
  }
  closed uv
}

// Declared groups: a tube used as a step, nested in a thicker group, a stretch used twice, a copy, and a group passed to a macro.
#declare TubeSteps = expressions { scale <1, 2, 1>  extrude { radius 0.5 } }
#declare ThickSteps = expressions { TubeSteps  displace 0.1 }
#declare Stretch = expressions { scale <1, sqrt(2), 1> }
#declare TaperSteps = expressions { extrude { radius function(uv) { 1 - 0.75*uv.v } } }
#declare CopiedSteps = TaperSteps;
#macro SkeinOf(Steps) skein { expressions { Steps } closed u  ends flat } #end
#declare SkeinTubeGroup = skein { expressions { TubeSteps } closed u  ends flat }
#declare SkeinThickGroup = SkeinOf(ThickSteps)
#declare SkeinFrustumGroup = skein { expressions { Stretch  Stretch  CopiedSteps } closed u  ends flat }

// Declared functions as values: a radius of v and a displacement by a pattern of pos, against the same written inline.
#declare TaperRadius = function(v) { 0.5 - 0.2*v }
#declare Bumps = function { pattern { bozo scale 0.3 } }
#declare SmallBumps = function { 0.05*Bumps(x, y, z) }
#declare SkeinDeclared = skein { expressions { scale <1, 2, 1>  extrude { radius TaperRadius }  displace SmallBumps } closed u }
#declare SkeinInline = skein {
  expressions { scale <1, 2, 1>  extrude { radius function(v) { 0.5 - 0.2*v } }  displace function(pos) { 0.05*Bumps(pos.x, pos.y, pos.z) } }
  closed u
}

// Crease: the unit square rolled a quarter turn onto a cylinder of radius 0.2 past x = 0.5, then running straight up.
#declare SkeinBent = skein { expressions { crease { axis path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.2  angle 90 } } }
