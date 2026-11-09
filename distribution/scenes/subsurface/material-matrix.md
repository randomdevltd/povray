# Subsurface materials: eight objects, five controlled versions

`material-matrix.pov` renders the complete comparison as a single scene. The
3600 × 5600 output includes row names, source descriptions, column controls,
cell coordinates, and a reading key. Its labels are scene geometry, so they
remain attached to the evidence rather than depending on a separate overlay.

The five columns hold geometry, transport scale, source power, normal detail,
lookup settings, camera and exposure fixed within each row:

| Column | Source | External fill | Sparse transparent coat | Surface export |
| --- | --- | --- | --- | --- |
| 1 External only | off | 1 | off | off |
| 2 Source + fill | on | 1 | off | off |
| 3 Source only | on | 0 | off | off |
| 4 Sparse coat | on | 1 | on | off |
| 5 Surface export | on | 1 | on | on |

Compare 1 with 2 to see the source contribution, 2 with 3 to see the contribution
of external lighting, 2 with 4 to see the surface coat, and 4 with 5 to see
surface export. The pale narrow panel beside every object is a diffuse receiver;
it makes light reaching an adjacent surface visible. A lit receiver in column 2
may already receive direct source light. Column 5 adds the object's outgoing
subsurface and bounced-light contributions; it does not uniquely isolate all receiver illumination.

The hand, panel and scroll contain patterned emitting media. The ear, fruit and
leaf use rear sources. The wax is illuminated by its flame and the membrane by
its central lamp source. The source description is printed beside each row.
Patterns define emitting density and surface decoration. Bulk scattering remains
homogeneous; this scene does not demonstrate heterogeneous transport coefficients.
The normal pattern remains fixed across the five columns and is not a separate
comparison variable.

The hand transport multiplier is `0.2`; other rows use their nominal `1.0`
multiplier. Hand source power is `0.02 lm`. Power is held
constant across the four source-enabled versions of a row. The hand emitter
domain requests 500,000 cells. Coating uses an independent narrow crackle network with an
opaque core and a short transition to transparency; it leaves most surface area
open and does not follow the embedded emitter pattern. Other source powers
and material settings are declared in `material-exploration.pov`.

Render from this directory with a build supporting the scene's subsurface and
media-light features, adding the repository's `distribution/include` to the
include path:

```sh
povray +Imaterial-matrix.pov +Omaterial-matrix.png +W3600 +H5600 +FN +PR +AM4 +A0.1 +WT2
```

A 900 × 1400 render is a framing proof only; it is too small to evaluate material
detail or read all labels. The full output has a minimum text scale of `0.0093`
scene units in a `0.96`-unit view, approximately 35 pixels per font em at 3600
pixels wide. The font's visible capital height is approximately 25 pixels.

`MatrixRow` (0–7) and `MatrixColumn` (0–4) select specimens while preserving
the full frame and labels. Their default `-1` selects every row or column.
`MatrixRow=-2` renders the labelled frame without specimens. Region renders
can use the same full output dimensions and camera to preserve pixel placement.
