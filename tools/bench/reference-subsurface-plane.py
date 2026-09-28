#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path


def transmission(cosine, eta):
    g2 = eta * eta + cosine * cosine - 1
    if cosine <= 0 or g2 <= 0:
        return 0.0
    g = math.sqrt(g2)
    f = 0.5 * ((g - cosine) / (g + cosine)) ** 2
    f *= 1 + ((cosine * (g + cosine) - 1) / (cosine * (g - cosine) + 1)) ** 2
    return 1 - min(1, max(0, f))


def profile(reflectance, eta, mfp):
    fdr = eta * eta - eta ** 2.25 if eta < 1 else (1 - 1 / eta) / 4 + 0.75 * (1 - 1 / eta) ** 4.5
    boundary = (1 + fdr) / (1 - fdr)
    lo, hi = 0.0, 1.0
    for _ in range(80):
        alpha = (lo + hi) / 2
        root = math.sqrt(3 * (1 - alpha))
        rd = alpha / 2 * (1 + math.exp(-4 / 3 * boundary * root)) * math.exp(-root)
        if rd < reflectance:
            lo = alpha
        else:
            hi = alpha
    alpha = (lo + hi) / 2
    zr = mfp * alpha
    return alpha / (4 * math.pi), math.sqrt(3 * (1 - alpha)) / zr, zr, zr * (1 + 4 / 3 * boundary)


def main():
    parser = argparse.ArgumentParser(description='Deterministic dipole integral for uniform irradiance on a planar rectangle.')
    parser.add_argument('output')
    parser.add_argument('--map', help='Native uniform map from make-photon-flux; reads quantized flux and incidence.')
    parser.add_argument('--step', type=float, default=0.05)
    parser.add_argument('--extent', type=float, default=8)
    parser.add_argument('--ior', type=float, default=1.4)
    parser.add_argument('--reflectance', type=float, default=0.8)
    parser.add_argument('--mfp', type=float, default=0.25)
    parser.add_argument('--mm', type=float, default=1)
    parser.add_argument('--width', type=int, default=64)
    parser.add_argument('--height', type=int, default=48)
    parser.add_argument('--angles', type=int, default=2048)
    args = parser.parse_args()
    flux, cosine = 1.0, 1.0
    if args.map:
        raw = Path(args.map).read_bytes()
        count = struct.unpack_from('i', raw)[0]
        if count <= 0 or len(raw) != 8 + count * 20:
            raise ValueError('expected a native surface-only map with 20-byte records')
        colour = raw[16:20]
        theta, phi = struct.unpack_from('bb', raw, 21)
        if len(set(colour[:3])) != 1:
            raise ValueError('expected neutral incident flux')
        for i in range(count):
            at = 4 + 20 * i
            if raw[at + 12:at + 16] != colour or struct.unpack_from('bb', raw, at + 17) != (theta, phi):
                raise ValueError('expected constant flux and incidence')
        flux = math.ldexp(colour[0], colour[3] - 258) / args.step ** 2
        cosine = math.sin(theta * math.pi / 127)
    scale, sigma, zr, zv = profile(args.reflectance, args.ior, args.mfp)
    factor = flux * transmission(cosine, args.ior) * transmission(6 / math.sqrt(36 + 1e-6), args.ior)
    angles = [(math.cos(2 * math.pi * (k + 0.5) / args.angles), math.sin(2 * math.pi * (k + 0.5) / args.angles)) for k in range(args.angles)]
    origin = math.exp(-sigma * zr) + math.exp(-sigma * zv)
    values = []
    for y in range(args.height):
        z = 3 * (0.5 - (y + 0.5) / args.height)
        for x in range(args.width):
            px = 4 * ((x + 0.5) / args.width - 0.5)
            total = 0.0
            for c, s in angles:
                rx = (args.extent - px) / c if c > 0 else (-args.extent - px) / c
                rz = (args.extent - z) / s if s > 0 else (-args.extent - z) / s
                r = min(rx, rz) * args.mm
                dr, dv = math.hypot(zr, r), math.hypot(zv, r)
                total += origin - zr / dr * math.exp(-sigma * dr) - zv / dv * math.exp(-sigma * dv)
            values.append(factor * scale * total * 2 * math.pi / args.angles)
    data = b''.join(struct.pack('>HHH', *([round(min(1, max(0, value)) * 65535)] * 3)) for value in values)
    Path(args.output).write_bytes(f'P6\n{args.width} {args.height}\n65535\n'.encode() + data)
    print(json.dumps({'mean': sum(values) / len(values), 'minimum': min(values), 'maximum': max(values), 'flux_factor': flux, 'angles': args.angles}))


if __name__ == '__main__':
    main()
