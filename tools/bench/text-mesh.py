# text-mesh.py <n> <out.inc>: an n by n vertex grid as a text mesh2, TextMesh (800 gives 59 MB).
import math, sys
n = int(sys.argv[1])
out = open(sys.argv[2], "w")
w = out.write
w("#declare TextMesh = mesh2 {\n  vertex_vectors { %d,\n" % (n * n))
for j in range(n):
    for i in range(n):
        x, z = i / (n - 1) * 2 - 1, j / (n - 1) * 2 - 1
        w("    <%.6f, %.6f, %.6f>,\n" % (x, 0.1 * math.sin(7 * x) * math.cos(5 * z), z))
w("  }\n  face_indices { %d,\n" % (2 * (n - 1) * (n - 1)))
for j in range(n - 1):
    for i in range(n - 1):
        a = j * n + i
        w("    <%d, %d, %d>, <%d, %d, %d>,\n" % (a, a + 1, a + n, a + 1, a + n + 1, a + n))
w("  }\n}\n")
