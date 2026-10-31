# Independent Cartesian volume quadrature: a white Lambertian receiver reads integral(j * V * T * cos / r^2)/pi.
# volume_transport_check(folder, subset) checks the images in folder; subset is all, lumens or photons.

function boxes(kind) {
    if (kind == 2 || kind == 3) return split("-0.73 -0.22 1.2 1.8 -0.3 0.3 0.31 0.81 1.2 1.8 -0.3 0.3", BOX, " ") / 6
    if (kind == 5) return split("-0.5 0.5 0 1 -0.5 0.5", BOX, " ") / 6
    return split("-0.5 0.5 1 2 -0.4 0.4", BOX, " ") / 6
}

# Sets PIGMENT[1..3] for a source cell at x.
function pigment(kind, x,    t) {
    if (kind == 4) { PIGMENT[1] = 1; PIGMENT[2] = 0; PIGMENT[3] = 0; return }
    if (kind == 2 || kind == 3) {
        t = (x + 1) / 2
        PIGMENT[1] = 1 - 0.95 * t; PIGMENT[2] = 0.2 + 0.2 * t; PIGMENT[3] = 0.05 + 0.95 * t
        return
    }
    PIGMENT[1] = PIGMENT[2] = PIGMENT[3] = 1
}

function coefficient(kind,    count, b, o, integral) {
    integral = 0
    count = boxes(kind)
    for (b = 0; b < count; b++) {
        o = 6 * b
        pigment(kind, (BOX[o + 1] + BOX[o + 2]) / 2)
        integral += (BOX[o + 2] - BOX[o + 1]) * (BOX[o + 4] - BOX[o + 3]) * (BOX[o + 6] - BOX[o + 5]) \
            * (PIGMENT[1] * 0.297 + PIGMENT[2] * 0.589 + PIGMENT[3] * 0.114)
    }
    return 10 / (4 * PI * integral)
}

# Fills result[] with the receiver radiance of every pixel; frame 2, camera 0.25 above the receiver.
# Source cells go to global arrays that are overwritten, not cleared: freeing big arrays slows mawk.
function reference(width, height, kind, sigma, elevation, gain, divisions, global_sigma, result,
                   count, n, b, o, x0, y0, z0, ddx, ddy, ddz, a, bb, c,
                   row, col, rz, rx, s, sy, dx, dz, distance, xa, xb, fraction, f, transmittance, weight, r, g, bl, k) {
    count = boxes(kind)
    n = 0
    for (b = 0; b < count; b++) {
        o = 6 * b
        x0 = BOX[o + 1]; y0 = BOX[o + 3]; z0 = BOX[o + 5]
        ddx = (BOX[o + 2] - x0) / divisions; ddy = (BOX[o + 4] - y0) / divisions; ddz = (BOX[o + 6] - z0) / divisions
        for (a = 0; a < divisions; a++) {
            pigment(kind, x0 + (a + 0.5) * ddx)
            for (bb = 0; bb < divisions; bb++)
                for (c = 0; c < divisions; c++) {
                    CX[n] = x0 + (a + 0.5) * ddx; CY[n] = y0 + (bb + 0.5) * ddy; CZ[n] = z0 + (c + 0.5) * ddz
                    CR[n] = PIGMENT[1]; CG[n] = PIGMENT[2]; CB[n] = PIGMENT[3]
                    DV[n++] = ddx * ddy * ddz
                }
        }
    }
    k = 0
    for (row = 0; row < height; row++) {
        rz = 2 * (0.5 - (row + 0.5) / height)
        for (col = 0; col < width; col++) {
            rx = top_down_x(col, width, 2)
            r = g = bl = 0
            for (s = 0; s < n; s++) {
                sy = CY[s] + elevation
                if (sy <= 0) continue
                dx = CX[s] - rx; dz = CZ[s] - rz
                distance = sqrt(dx * dx + sy * sy + dz * dz)
                if (kind == 3) {
                    xa = rx + dx * 0.49 / sy
                    xb = rx + dx * 0.51 / sy
                    if ((xa < xb ? xa : xb) <= 0.13 && (xa > xb ? xa : xb) >= -0.13) continue
                }
                transmittance = 1.0
                if (sigma) {
                    fraction = (CY[s] - 1) / sy
                    if (dx) { f = (dx > 0 ? CX[s] + 0.5 : 0.5 - CX[s]) / abs(dx); if (f < fraction) fraction = f }
                    if (dz) { f = (dz > 0 ? CZ[s] + 0.4 : 0.4 - CZ[s]) / abs(dz); if (f < fraction) fraction = f }
                    transmittance = exp(-sigma * fraction * distance)
                }
                transmittance *= exp(-global_sigma * (distance + 0.25))
                weight = gain * transmittance * sy * DV[s] / (PI * distance ^ 3)
                r += CR[s] * weight; g += CG[s] * weight; bl += CB[s] * weight
            }
            result[k++] = r; result[k++] = g; result[k++] = bl
        }
    }
    return k
}

function asinh(v) {
    if (v < 0) return -asinh(-v)
    return log(v + sqrt(v * v + 1))
}

function rectangle_potential(x, z, depth,    radius) {
    radius = sqrt(x * x + z * z + depth * depth)
    return x * asinh(z / sqrt(x * x + depth * depth)) + z * asinh(x / sqrt(z * z + depth * depth)) - depth * atan2(x * z, depth * radius)
}

# Integrates y/r^3 in y analytically, then the rectangular 1/r potential, between two source planes.
function slab_reference(width, height, frame, x_low, x_high, near, far, strength, result,    row, col, z, x, plane, depth, value, k, xx, zz, sx, sz, v) {
    k = 0
    for (row = 0; row < height; row++) {
        z = frame * (0.5 - (row + 0.5) / height)
        for (col = 0; col < width; col++) {
            x = top_down_x(col, width, frame)
            for (plane = 0; plane < 2; plane++) {
                depth = plane ? far : near
                value = 0
                for (sx = -1; sx <= 1; sx += 2)
                    for (sz = -1; sz <= 1; sz += 2) {
                        xx = (sx < 0 ? x_low : x_high) - x
                        zz = (sz < 0 ? -0.5 : 0.5) - z
                        value += sx * sz * rectangle_potential(xx, zz, depth)
                    }
                v[plane] = value
            }
            result[k] = result[k + 1] = result[k + 2] = strength * (v[0] - v[1]) / PI
            k += 3
        }
    }
    return k
}

function cube_reference(width, height, gap, probe, result) {
    return slab_reference(width, height, 0.02, -0.5, 0.5, max2(0, gap - probe), 1 + gap - probe, 0.15, result)
}

function load(name, v) {
    return read_ppm(VOLUME_FOLDER "/" name ".ppm", v)
}

function volume_transport_check(folder, subset,    n, nc, nf, nw, w, h, white_j, count, names, kinds, sigmas, elevations, fogs,
                                i, kind, gain, pixels, coarse, fine, white, quarter, gaps, nt, target, na, adjacent, nh, half_box) {
    PI = atan2(0, -1)
    VOLUME_FOLDER = folder
    if (subset == "photons") {
        n = load("photon_gathered", pixels)
        nc = reference(IMAGE_W, IMAGE_H, 1, 0, 0, coefficient(1), 20, 0, coarse)
        nf = reference(IMAGE_W, IMAGE_H, 1, 0, 0, coefficient(1), 32, 0, fine)
        compare("photon reference convergence", coarse, nc, fine, nf, 0.025)
        compare("physical source photon irradiance", pixels, n, fine, nf, 0.08)
        return
    }
    nw = load("white", white)
    w = IMAGE_W; h = IMAGE_H
    white_j = coefficient(1)
    count = split("white red absorbed shaped shadow distant remote fog atmosphere_box", names, " ")
    split("1 4 1 2 3 1 1 1 1", kinds, " ")
    split("0 0 0.7 0 0 0 0 0 0", sigmas, " ")
    split("0 0 0 0 0 20 100000 0 0", elevations, " ")
    split("0 0 0 0 0 0 0 0.7 0.7", fogs, " ")
    for (i = 1; i <= count; i++) {
        kind = kinds[i] + 0
        gain = kind == 1 ? white_j : coefficient(kind)
        if (names[i] == "distant") gain = 100 * white_j
        if (names[i] == "remote") gain = 100000000 * white_j
        n = load(names[i], pixels)
        if (IMAGE_W != w || IMAGE_H != h) fail(names[i] ": " IMAGE_W "x" IMAGE_H " differs from white " w "x" h)
        nc = reference(w, h, kind, sigmas[i] + 0, elevations[i] + 0, gain, 16, fogs[i] + 0, coarse)
        nf = reference(w, h, kind, sigmas[i] + 0, elevations[i] + 0, gain, 24, fogs[i] + 0, fine)
        compare(names[i] " reference convergence", coarse, nc, fine, nf, 0.025)
        compare(names[i], pixels, n, fine, nf, 0.08)
    }
    n = load("sparse", pixels)
    compare("sparse transport scaling", pixels, n, white, nw, 0.10)
    n = load("scaled", pixels)
    for (i = 0; i < nw; i++) quarter[i] = white[i] / 4
    compare("scaled transport scaling", pixels, n, quarter, nw, 0.06)
    if (subset == "lumens") return
    count = split("0 0.02 0.1 0.5 1 2 4 8 16", gaps, " ")
    for (i = 1; i <= count; i++) {
        n = load("cube_" gaps[i], pixels)
        nt = cube_reference(w, h, gaps[i] + 0, 0, target)
        compare("cube_" gaps[i] " mean-chord reference", pixels, n, target, nt, 0.08)
    }
    n = load("cube_center", pixels)
    nt = cube_reference(w, h, 0, 0.5, target)
    compare("cube center", pixels, n, target, nt, 0.08)
    n = load("cube_scaled", pixels)
    na = load("cube_0", adjacent)
    compare("nits geometric scaling", pixels, n, adjacent, na, 0.06)
    nh = load("half_box", half_box)
    n = load("clipped_box", pixels)
    nt = slab_reference(w, h, 2, -0.5, 0, 1, 2, 0.2, target)
    compare("half box mean-chord reference", half_box, nh, target, nt, 0.08)
    compare("clipped box mean-chord reference", pixels, n, target, nt, 0.08)
    compare("clipped and explicit source equivalence", pixels, n, half_box, nh, 0.06)
}
