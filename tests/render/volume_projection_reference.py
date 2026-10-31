#!/usr/bin/env python3
"""Experimental alternative: projected silhouettes of one or two axis-aligned boxes.

Convex hulls and polygon clipping provide a general two-box reference.
Boxes sharing a cross-section also admit an exact overlap expression.
This helper is not imported by the active isotropic transport oracle.
"""

import math


def polygon_area(vertices):
    return abs(sum(a[0] * b[1] - a[1] * b[0]
                   for a, b in zip(vertices, vertices[1:] + vertices[:1]))) / 2


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def convex_hull(points):
    ordered = sorted(set(points))
    chains = []
    for sequence in [ordered, list(reversed(ordered))]:
        chain = []
        for point in sequence:
            while len(chain) >= 2 and cross(chain[-2], chain[-1], point) <= 0:
                chain.pop()
            chain.append(point)
        chains.append(chain)
    return chains[0][:-1] + chains[1][:-1]


def intersection(subject, clip):
    for a, b in zip(clip, clip[1:] + clip[:1]):
        output = []
        for p, q in zip(subject, subject[1:] + subject[:1]):
            cp, cq = cross(a, b, p), cross(a, b, q)
            if cp >= 0:
                output.append(p)
            if (cp >= 0) != (cq >= 0):
                t = cp / (cp - cq)
                output.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
        subject = output
        if not subject:
            break
    return subject


def projected_polygon(box, direction):
    x, y, z = direction
    length = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / length, y / length, z / length
    horizontal = math.hypot(x, y)
    u = (-y / horizontal, x / horizontal, 0) if horizontal else (1, 0, 0)
    v = (y * u[2] - z * u[1], z * u[0] - x * u[2], x * u[1] - y * u[0])
    corners = [(a, b, c) for a in box[:2] for b in box[2:4] for c in box[4:6]]
    return convex_hull([(sum(a * b for a, b in zip(p, u)), sum(a * b for a, b in zip(p, v)))
                        for p in corners])


class Projection:
    def __init__(self, boxes):
        self.boxes = boxes
        self.dimensions = [(b[1] - b[0], b[3] - b[2], b[5] - b[4]) for b in boxes]
        self.gap = None
        self._mean = None
        if len(boxes) == 2 and boxes[0][2:] == boxes[1][2:]:
            self.gap = max(boxes[0][0], boxes[1][0]) - min(boxes[0][1], boxes[1][1])
            assert self.gap >= 0

    def polygon_union(self, direction):
        polygons = [projected_polygon(box, direction) for box in self.boxes]
        total = sum(polygon_area(p) for p in polygons)
        if len(polygons) == 2:
            total -= polygon_area(intersection(*polygons))
        return total

    def area(self, x, y, z):
        u, v, w = abs(x), abs(y), abs(z)
        if len(self.boxes) > 1 and self.gap is None:
            return self.polygon_union((x, y, z))
        total = sum(b * c * u + a * c * v + a * b * w for a, b, c in self.dimensions)
        if self.gap is not None and u:
            _, b, c = self.dimensions[0]
            total -= u * max(0, b - self.gap * v / u) * max(0, c - self.gap * w / u)
        return total

    def quadrature_mean(self, divisions):
        total = 0
        for i in range(divisions):
            u = (i + 0.5) / divisions
            radius = math.sqrt(1 - u * u)
            for k in range(2 * divisions):
                phi = (k + 0.5) * math.pi / (4 * divisions)
                total += self.area(u, radius * math.cos(phi), radius * math.sin(phi))
        return total / (2 * divisions * divisions)

    @property
    def mean(self):
        if self._mean is None:
            if len(self.boxes) == 1:
                a, b, c = self.dimensions[0]
                self._mean = (a * b + a * c + b * c) / 2
            else:
                coarse = self.quadrature_mean(128)
                self._mean = self.quadrature_mean(256)
                assert abs(coarse / self._mean - 1) < 0.0002
        return self._mean

    def self_check(self):
        for direction in [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 2, 3), (3, 1, 1),
                          (10, 1, 2), (2, 9, 4), (2, -3, 5), (9, 3, -2)]:
            length = math.sqrt(sum(v * v for v in direction))
            unit = tuple(v / length for v in direction)
            assert abs(self.area(*unit) - self.polygon_union(unit)) < 1e-10
