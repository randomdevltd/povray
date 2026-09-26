# mesh-cylinder.py <nu> <nv> <out.inc>: a bumpy cylinder as mesh2 M with a normal per vertex, 2*nu*(nv-1) triangles.
import math, sys
nu, nv, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
def pos(i, j):
    a = 2 * math.pi * i / nu; y = 4.0 * j / (nv - 1)
    r = 1.0 + 0.08 * math.sin(7 * a + 3 * y) * math.cos(5 * y - 2 * a) + 0.03 * math.sin(41 * a + 29 * y)
    return (r * math.cos(a), y, r * math.sin(a))
V = [pos(i, j) for j in range(nv) for i in range(nu)]
def nrm(i, j):
    e = 1e-3
    p = pos(i, j); pu = pos(i + e, j); pv = pos(i, j + e) if j < nv - 1 else pos(i, j - e)
    du = [pu[k] - p[k] for k in range(3)]; dv = [pv[k] - p[k] for k in range(3)]
    if j == nv - 1: dv = [-x for x in dv]
    n = (du[1]*dv[2]-du[2]*dv[1], du[2]*dv[0]-du[0]*dv[2], du[0]*dv[1]-du[1]*dv[0])
    l = math.sqrt(sum(x*x for x in n)); return tuple(x / l for x in n)
with open(out, 'w') as f:
    f.write('#declare M = mesh2 {\n vertex_vectors { %d,\n' % len(V))
    f.write(',\n'.join('<%.6f,%.6f,%.6f>' % v for v in V))
    f.write('\n }\n normal_vectors { %d,\n' % len(V))
    f.write(',\n'.join('<%.5f,%.5f,%.5f>' % nrm(i, j) for j in range(nv) for i in range(nu)))
    T = []
    for j in range(nv - 1):
        for i in range(nu):
            a = j * nu + i; b = j * nu + (i + 1) % nu; c = a + nu; d = b + nu
            T.append((a, b, d)); T.append((a, d, c))
    f.write('\n }\n face_indices { %d,\n' % len(T))
    f.write(',\n'.join('<%d,%d,%d>' % t for t in T))
    f.write('\n }\n}\n')
print(len(T), 'triangles', len(V), 'vertices')
