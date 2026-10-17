#!/usr/bin/env python3
"""Score subsurface renders of tools/bench/sslt-reference.pov against tools/bench/sslt-reference.cpp's random walks.

  sslt-reference.py commands DIR [--povray P] [--reference R] [--scene S] [--tag T --declare K=V...] [--entry beam] [--media M,...]
  sslt-reference.py score DIR [--tag T] [--entry beam] [--medium M] [--sheets] [--region RUN:X0,Y0,X1,Y1...] [--calibrate RUN=OTHER...]

commands prints one command per line: the walks to DIR/<run>[-beam].pfm and the render to DIR/<run>[-T].png; score reads
them. --entry beam walks with the light's refracted beams (exact where they enter through flat faces; see doc/PERF.md),
--only RUN,... keeps some runs. --media walks each run once for several media, to DIR/<run>-<medium>.pfm: "classic", the
renderer's table, or one of MEDIA, the reduced albedos method 2 derives for these materials with each profile (doc/PERF.md);
score --medium M scores against them; --normalise-exit divides the renders by 1 - F̄dr from outside (doc/PERF.md).

Errors are in levels of 255 (sRGB) over the pixels both images cover: rms less the reference's own noise, mean, and the
worst 8x8 block's mean luminance. "Absolute" compares as rendered; "geometry" first scales the reference per channel by
the renderer's ratio to it on the thick slab of the same material (or on OTHER), so only what the shape changes is left.
The studs' table in doc/PERF.md is `score DIR --calibrate studs-red=bare-red --calibrate studs-high=bare-high`.
"""
import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

MATERIALS = ['red', 'high', 'wax', 'bark', 'clay']
RUNS = [
    ('slab-red', 0, 0, {}), ('slab-high', 0, 1, {}), ('slab-wax', 0, 2, {}), ('slab-bark', 0, 3, {}), ('slab-clay', 0, 4, {}),
    ('head-on-red', 0, 0, {'View': 1}), ('head-on-high', 0, 1, {'View': 1}),
    ('notch-wax', 1, 2, {}), ('notch-high', 1, 1, {}),
    ('trunk-bark', 2, 3, {}),
    ('wall-clay', 3, 4, {}), ('wall-high', 3, 1, {}),
    ('bare-red', 4, 0, {'Bare': 1}), ('bare-high', 4, 1, {'Bare': 1}), ('studs-red', 4, 0, {}), ('studs-high', 4, 1, {}),
]
WIDTH, HEIGHT, BLOCK, WALKS = 160, 120, 8, 2048
IORS = [1.5, 1.5, 1.45, 1.4, 1.4]
MEDIA = {
    'fdr': ['0.993424403,0.563735471,0.438775771', '0.999705255', '0.981075651,0.931631333,0.492624062',
            '0.950724539,0.906538880,0.826884697', '0.991213739,0.961276178,0.826884697'],
    'better': ['0.993651318,0.721194076,0.602064304', '0.999676585', '0.983089631,0.949960891,0.635634404',
               '0.959504423,0.930223763,0.880017943', '0.991275012,0.967060688,0.880017943'],
    'pbd': ['0.994886087,0.778190186,0.694543893', '0.999850869', '0.985890453,0.957188908,0.717735052',
            '0.965729235,0.940729773,0.899182265', '0.993132829,0.972251358,0.899182265'],
}


def commands(args):
    out = Path(args.dir)
    for name, shape, material, extra in RUNS:
        if args.only and name not in args.only.split(','):
            continue
        keys = {'Shape': shape, 'Material': material, **extra}
        ref = ' '.join(f'{k}={v}' for k, v in keys.items()) + (' Entry=beam' if args.entry == 'beam' else '')
        if args.media:
            media = args.media.split(',')
            albedo = ':'.join('table' if m == 'classic' else MEDIA[m][material] for m in media)
            outs = ','.join(f'{out / walked(name, args.entry, m)}.pfm' for m in media)
            errs = ','.join(f'{out / walked(name, args.entry, m)}-err.pfm' for m in media)
            print(f'{args.reference} {ref} W={WIDTH} H={HEIGHT} Walks={args.walks} Albedo={albedo} Out={outs} Err={errs}')
        else:
            walks = out / walked(name, args.entry)
            print(f'{args.reference} {ref} W={WIDTH} H={HEIGHT} Walks={args.walks} Out={walks}.pfm Err={walks}-err.pfm')
        declares = ' '.join([f'Declare={k}={v}' for k, v in keys.items()] + [f'Declare={d}' for d in args.declare])
        print(f'{args.povray} +I{args.scene} +W{WIDTH} +H{HEIGHT} -A +FN16 File_Gamma=1.0 +UA {declares} +O{out / rendered(name, args.tag)}.png')


def rendered(name, tag):
    return f'{name}-{tag}' if tag else name


def walked(name, entry, medium=''):
    return f'{name}-{medium}' if medium else f'{name}-{entry}' if entry == 'beam' else name


def read_pfm(path):
    raw = Path(path).read_bytes()
    head = raw.split(b'\n', 3)
    width, height = map(int, head[1].split())
    scale = float(head[2])
    values = struct.unpack(('<' if scale < 0 else '>') + f'{width * height * 3}f', head[3][:width * height * 12])
    rows = [values[(height - 1 - y) * width * 3:(height - y) * width * 3] for y in range(height)]
    return width, height, [[tuple(row[x * 3:x * 3 + 3]) for x in range(width)] for row in rows]


def read_png(path):
    raw = Path(path).read_bytes()
    at, idat, header = 8, b'', None
    while at < len(raw):
        length, kind = struct.unpack('>I4s', raw[at:at + 8])
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', raw[at + 8:at + 21])
        elif kind == b'IDAT':
            idat += raw[at + 8:at + 8 + length]
        at += 12 + length
    width, height, depth, colour = header[:4]
    channels = {2: 3, 6: 4}[colour]
    size = depth // 8
    bpp, stride = channels * size, width * channels * size
    data, prev, rows = zlib.decompress(idat), bytearray(stride), []
    for y in range(height):
        kind, line = data[y * (stride + 1)], bytearray(data[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    top = (1 << depth) - 1
    unpack = (lambda row, i: int.from_bytes(row[i * size:(i + 1) * size], 'big') / top)
    return width, height, [[tuple(unpack(row, x * channels + k) for k in range(channels)) for x in range(width)] for row in rows]


def transmittance(eta, steps=20000):
    """2 times the integral of the Fresnel transmittance into eta times the cosine: what Ft's directional term leaves of unit exitance."""
    total = 0.0
    for i in range(steps):
        c = (i + 0.5) / steps
        g = math.sqrt(eta * eta + c * c - 1)
        f = 0.5 * ((g - c) / (g + c)) ** 2 * (1 + ((c * (g + c) - 1) / (c * (g - c) + 1)) ** 2)
        total += (1 - f) * 2 * c / steps
    return total


def srgb(v):
    v = min(1.0, max(0.0, v))
    return 255 * (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)


def slope(v):
    v = min(1.0, max(1e-6, v))
    return 255 * (12.92 if v <= 0.0031308 else 1.055 / 2.4 * v ** (1 / 2.4 - 1))


def load(directory, name, tag, entry, medium=''):
    _, _, render = read_png(Path(directory) / f'{rendered(name, tag)}.png')
    _, _, reference = read_pfm(Path(directory) / f'{walked(name, entry, medium)}.pfm')
    _, _, error = read_pfm(Path(directory) / f'{walked(name, entry, medium)}-err.pfm')
    return render, reference, error


def pixels(render, reference, error):
    both, mismatched = [], 0
    for y, row in enumerate(render):
        for x, r in enumerate(row):
            seen, walked = r[3] > 0.5, error[y][x][0] >= 0.0
            mismatched += seen != walked
            if seen and walked:
                both.append((x, y, r[:3], reference[y][x], error[y][x]))
    return both, mismatched


def score(both, gain=(1.0, 1.0, 1.0)):
    total = noise = mean = 0.0
    blocks = {}
    for x, y, r, w, e in both:
        luma = 0.0
        for j, k in enumerate((0.2126, 0.7152, 0.0722)):
            d = srgb(r[j]) - srgb(w[j] * gain[j])
            total += d * d
            noise += (e[j] * gain[j] * slope(w[j] * gain[j])) ** 2
            mean += d
            luma += k * d
        block = blocks.setdefault((x // BLOCK, y // BLOCK), [0.0, 0])
        block[0] += luma
        block[1] += 1
    count = 3 * len(both)
    worst = max((b[0] / b[1] for b in blocks.values() if b[1] >= BLOCK * BLOCK // 2), key=abs, default=0.0)
    return math.sqrt(max(0.0, total - noise) / count), math.sqrt(noise / count), mean / count, worst


def means(both):
    return [sum(p[2][j] for p in both) / len(both) for j in range(3)], [sum(p[3][j] for p in both) / len(both) for j in range(3)]


def write_sheet(path, render, reference, gain):
    rows = []
    for y, row in enumerate(render):
        line = bytearray()
        for part in range(3):
            for x, r in enumerate(row):
                w = [reference[y][x][j] * gain[j] for j in range(3)]
                v = [srgb(r[j]) for j in range(3)] if part == 0 else [srgb(w[j]) for j in range(3)] if part == 1 else \
                    [128 + 4 * (srgb(r[j]) - srgb(w[j])) for j in range(3)]
                line += bytes(int(min(255, max(0, round(c)))) for c in v)
        rows.append(b'\0' + bytes(line))
    width, height = len(render[0]) * 3, len(render)
    chunk = (lambda kind, data: struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data)))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def report(args):
    gains, ratios = {}, {}
    print('| Run | Pixels (mismatched) | Renderer / walks, mean per channel | Absolute rms / mean / worst block | Geometry rms / mean / worst block | Walk noise |')
    print('|---|---|---|---|---|---|')
    for name, shape, material, extra in RUNS:
        show = not args.only or name in args.only.split(',')
        if not show and (shape or extra) and name not in [c.split('=')[1] for c in args.calibrate]:
            continue
        if not (Path(args.dir) / f'{rendered(name, args.tag)}.png').exists() or not (Path(args.dir) / f'{walked(name, args.entry, args.medium)}.pfm').exists():
            print(f'| {name} | missing | | | | |' if show else '', end='\n' if show else '')
            continue
        render, reference, error = load(args.dir, name, args.tag, args.entry, args.medium)
        if args.normalise_exit:
            unit = transmittance(IORS[material])
            render = [[(r[0] / unit, r[1] / unit, r[2] / unit, r[3]) for r in row] for row in render]
        both, mismatched = pixels(render, reference, error)
        drawn, seen = means(both)
        ratio = [drawn[j] / seen[j] if seen[j] > 0 else 0.0 for j in range(3)]
        if shape == 0 and not extra:
            gains[material] = ratio
        ratios[name] = ratio
        gain = ratios.get(dict(c.split('=') for c in args.calibrate).get(name), gains.get(material, (1.0, 1.0, 1.0)))
        boxes = ([(name, both)] if show else []) + [(f'{name} {box}', [q for q in both if x0 <= q[0] < x1 and y0 <= q[1] < y1])
                                  for run, box in (r.split(':') for r in args.region) if run == name
                                  for x0, y0, x1, y1 in [map(int, box.split(','))]]
        for label, part in boxes:
            rms, noise, mean, worst = score(part)
            grms, _, gmean, gworst = score(part, gain)
            print(f'| {label} | {len(part)} ({mismatched}) | {", ".join(f"{r:.3f}" for r in ratio)} | {rms:.2f} / {mean:+.2f} / {worst:+.1f} | '
                  f'{grms:.2f} / {gmean:+.2f} / {gworst:+.1f} | {noise:.2f} |')
        if args.sheets and show:
            write_sheet(Path(args.dir) / f'{rendered(name, args.tag)}-sheet.png', render, reference, gain)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('commands')
    c.add_argument('dir')
    c.add_argument('--povray', default='povray')
    c.add_argument('--reference', default='./sslt-reference')
    c.add_argument('--scene', default='tools/bench/sslt-reference.pov')
    c.add_argument('--tag', default='', help='renders go to <run>-<tag>.png')
    c.add_argument('--declare', action='append', default=[], help='K=V for every render, e.g. Method=1')
    c.add_argument('--media', default='', help='walk once for these media: classic and names in MEDIA')
    c.add_argument('--walks', type=int, default=WALKS)
    for each in (c, s := sub.add_parser('score')):
        each.add_argument('--entry', default='diffuse', choices=['diffuse', 'beam'])
        each.add_argument('--only', default='')
    s.add_argument('dir')
    s.add_argument('--tag', default='')
    s.add_argument('--medium', default='', help='score against DIR/<run>-<medium>.pfm')
    s.add_argument('--normalise-exit', action='store_true', help='divide renders by the hemispherical transmittance from outside')
    s.add_argument('--sheets', action='store_true', help='write <run>-sheet.png: render, reference, difference x4')
    s.add_argument('--region', action='append', default=[], help='RUN:X0,Y0,X1,Y1, a pixel box of one run scored on its own')
    s.add_argument('--calibrate', action='append', default=[], help='RUN=OTHER: scale by an earlier run instead of the slab')
    args = parser.parse_args()
    commands(args) if args.command == 'commands' else report(args)


if __name__ == '__main__':
    sys.exit(main())
