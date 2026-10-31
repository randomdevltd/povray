# Certified geometry bounds

Bounding boxes carry separate evidence for finite surface geometry and a finite
base, non-inverted `Inside` domain. A disc has a finite surface but its `Inside`
predicate describes a halfspace; a height field similarly extends downward. Empty
`Inside` predicates are bounded. Consumers apply object inversion separately.
Unknown geometry stays uncertified even when its acceleration box happens to
contain finite sentinel numbers. The legacy `INFINITE_FLAG` remains an
acceleration heuristic and does not decide whether a prepared interior is finite.

Primitive bounds, copies, affine transforms, CSG aggregation, clipping and prepared
publication preserve this evidence. A union needs finite surface bounds for every
child, and certified child interiors with no inverted children to certify its
interior. An intersection can use any non-inverted certified interior enclosure;
a difference uses its first operand. `bounded_by` cannot establish a finite
interior, while `clipped_by` can supply an actual finite domain. Existing uncertain
quadric bounds remain uncertified until a finite clipping domain supplies proof.

Meshes with an inside vector need a closed topology certificate. Exact coordinate
keys weld coincident positions, normalize signed zero, and reject nonfinite
vertices. Every undirected edge must occur exactly twice; degenerate or uncertain
topology stays uncertified. Shared mesh data computes this evidence once with
`call_once`. Certified closed meshes reject outside-box interior queries, including
ambiguous edge parity rays. Copies reuse the certificate. Generated mesh cache
format 2 stores geometry and rebuilds bounds and topology evidence on load; it does
not deserialize bounding-box provenance.

Prepared fields require certified interior bounds with finite, positive,
representable extents. There is no absolute coordinate, length or volume cutoff.
The same validation protects eager planning, grid allocation and final
publication. A cached layout must still match the current geometry bounds before
publication. Framing uses the separate finite surface enclosure and can retain
flat targets.

On an ordinary float/64-bit build, the two evidence flags increase `BoundingBox`
from 24 to 28 bytes; pointer BVH nodes remain 40 bytes because they already had
padding. A double-coordinate counterpart grows from 48 to 56 bytes. Mesh data adds
one `once_flag` and one integer per shared mesh; topology checking uses temporary
linear storage for welded indices and triangle edges, with sorting costs.

The halfspace bound solver accepts only intersections of plane primitives. It
keeps the existing fallback for other CSG, inverted object flags, uncertain
geometry, empty regions and regions without a strict interior.
Plane inversion normally negates the plane equation itself; that equation is
handled directly. Affine inverse transforms are applied to the entire local
equation. The bounding inequalities include the local `Plane::Inside` allowance;
the strict interior witness uses the unexpanded equations.

For each signed coordinate vector q, the solver searches for nonnegative weights
lambda on three normals. The weights are ordinary finite floating point numbers,
used as fixed exact inputs to the certificate. Outward interval arithmetic bounds
the residual r = q - sum(lambda*n) and B = sum(lambda*b). Every feasible point x
then satisfies q.x <= B + norm1(r)*M, where M = max(abs(x)). If all six residual
norms are at most rho < 1, the coordinate attaining M proves
M <= max(B)/(1-rho). Substituting this finite radius into each certificate gives
the six coordinate bounds. This is an explicit unboundedness exclusion; merely
finding vertices would not establish it. Failure to find certificates retains the
existing fallback.

Triple intersections propose weights and an interior witness only. Uncertain
determinants can be skipped because vertex enumeration never supplies a bound.
Either the certified box centre or a proposed vertex centroid must independently
satisfy every unexpanded inequality strictly, using interval predicates. Thus omitted or infeasible candidates cannot
make an empty or lower-dimensional region pass. All arithmetic feeding the bound
uses outward intervals. Bounds are converted to stored float origins and sizes
with outward rounding for both float and double endpoint consumers.

Elementary operations use volatile stores to prevent contraction and reassociation.
Subnormal inputs and results widen to an interval with normal endpoints, preserving
enclosure when fast math enables flush-to-zero or denormals-are-zero arithmetic.
Nonfinite intermediate intervals propagate as unknown rather than entering a
certificate.

The search prioritizes normals near the signed coordinate axes and progressively
enlarges its candidate basis. It stops as soon as six certificates and an interior
witness succeed; every witness still checks every input constraint. There is no
plane-count cutoff. Basis selection and successful small-basis validation are
linear in the input count. Exhaustive fallback is fourth-order in the worst case.
It runs during bounding, not ray tracing. The standalone unit reports preparation
cost for 6, 64 and 256 planes and covers transformed prisms, an open box, an
unbounded wedge with finite vertices, empty and flat regions, coefficient
uncertainty, and nonfinite input. Geometry units cover multiple scales, copied and
cached meshes, clipping, and the surface/interior distinction.
