# media_infinite_joint_check(image): finite-box single scattering on an infinite camera ray with a tail bound.

function infinite_init(    i) {
    PI = atan2(0, -1)
    split("1 .5 .2", COLOR, " "); split(".04 .03 .02", SCATTER, " "); split(".24 .43 .62", EXTINCTION, " ")
    split(".1834346424956498 .5255324099163290 .7966664774136267 .9602898564975363", NODES, " ")
    split(".3626837833783620 .3137066458778873 .2223810344533745 .1012285362903763", WEIGHTS, " ")
    split("0 4 .5 4 8 1 8 16 2 16 32 4 32 80 8", INTERVALS, " ")
    SIGMA_LOWER = .2
    DEPTH = 80
    LUMINANCE = COLOR[1] * .297 + COLOR[2] * .589 + COLOR[3] * .114
}

# Fills t[] and weight[] with Gauss-Legendre nodes along the camera ray; returns the count.
function camera_nodes(coarse, t, weight,    n, interval, start, end, width, count, i, j, midpoint, half) {
    n = 0
    for (interval = 0; interval < 5; interval++) {
        start = INTERVALS[3 * interval + 1]; end = INTERVALS[3 * interval + 2]; width = INTERVALS[3 * interval + 3]
        if (coarse) width *= 2
        count = int((end - start) / width)
        for (i = 0; i < count; i++) {
            midpoint = start + (i + .5) * width
            half = width / 2
            for (j = 1; j <= 4; j++) {
                t[n] = midpoint - half * NODES[j]; weight[n++] = half * WEIGHTS[j]
                t[n] = midpoint + half * NODES[j]; weight[n++] = half * WEIGHTS[j]
            }
        }
    }
    return n
}

function infinite_reference(x, y, divisions, coarse, total,    count, i, j, k, t, weight, nodes, coefficient,
                            node, w, z, s, dx, dy, dz, radius2, path, e1, e2, e3, a1, a2, a3, total0, total1, total2) {
    count = 0
    for (i = 0; i < divisions; i++)
        for (j = 0; j < divisions; j++)
            for (k = 0; k < divisions; k++) {
                SOURCE_X[count] = 1 + .2 * (i + .5) / divisions
                SOURCE_Y[count] = -.1 + .2 * (j + .5) / divisions
                SOURCE_Z[count++] = .35 + .3 * (k + .5) / divisions
            }
    nodes = camera_nodes(coarse, t, weight)
    coefficient = 100 / (16 * PI * PI * LUMINANCE * count)
    e1 = EXTINCTION[1]; e2 = EXTINCTION[2]; e3 = EXTINCTION[3]
    a1 = SCATTER[1] * COLOR[1]; a2 = SCATTER[2] * COLOR[2]; a3 = SCATTER[3] * COLOR[3]
    total0 = total1 = total2 = 0
    for (node = 0; node < nodes; node++) {
        z = -1 + t[node]
        w = weight[node]
        for (s = 0; s < count; s++) {
            dx = SOURCE_X[s] - x; dy = SOURCE_Y[s] - y; dz = SOURCE_Z[s] - z
            radius2 = dx * dx + dy * dy + dz * dz
            path = t[node] + sqrt(radius2)
            total0 += w * exp(-e1 * path) / radius2 * a1 * coefficient
            total1 += w * exp(-e2 * path) / radius2 * a2 * coefficient
            total2 += w * exp(-e3 * path) / radius2 * a3 * coefficient
        }
    }
    total[0] = total0; total[1] = total1; total[2] = total2
}

function media_infinite_joint_check(image,    fine, coarse, worst, c, convergence, tail, n, measured, k, row, col, rgb, target,
                                    pass, mean, ref, rms, i, limit, ok) {
    infinite_init()
    infinite_reference(0, 0, 12, 0, fine)
    infinite_reference(0, 0, 6, 1, coarse)
    worst = 0
    for (c = 0; c < 3; c++) {
        convergence[c] = abs(fine[c] - coarse[c]) / fine[c]
        if (convergence[c] > worst) worst = convergence[c]
        tail[c] = SCATTER[c + 1] * (100 * COLOR[c + 1] / (4 * PI * LUMINANCE)) / (4 * PI * .99 ^ 2) * exp(-SIGMA_LOWER * DEPTH) / SIGMA_LOWER
    }
    printf "camera limit %g: centre %.9g %.9g %.9g, coarse %.9g %.9g %.9g, relative convergence %.3g %.3g %.3g\n",
        DEPTH, fine[0], fine[1], fine[2], coarse[0], coarse[1], coarse[2], convergence[0], convergence[1], convergence[2]
    printf "omitted tail upper bound %.3g %.3g %.3g (extinction >= .2, source-to-ray separation >= .99, isotropic phase)\n", tail[0], tail[1], tail[2]
    if (worst > .005) fail("independent camera/source quadrature has not converged: relative " worst " > 0.005")
    n = read_ppm(image, measured)
    k = 0
    for (row = 0; row < IMAGE_H; row++)
        for (col = 0; col < IMAGE_W; col++) {
            infinite_reference(.02 * ((col + .5) / IMAGE_W - .5), .02 * (.5 - (row + .5) / IMAGE_H), 12, 0, rgb)
            target[k++] = rgb[0]; target[k++] = rgb[1]; target[k++] = rgb[2]
        }
    pass = 1
    for (c = 0; c < 3; c++) {
        mean = ref = rms = 0
        for (i = c; i < n; i += 3) { mean += measured[i]; ref += target[i]; rms += (measured[i] - target[i]) ^ 2 }
        mean /= n / 3; ref /= n / 3; rms = sqrt(rms / (n / 3))
        limit = max2(2 / 65535, .03 * ref)
        ok = abs(mean - ref) <= limit
        pass = pass && ok
        printf "%s channel %d: mean %.9g, reference %.9g, limit %.9g, relative bias %.6g, relative pixel rms %.6g, %s\n",
            image, c, mean, ref, limit, (mean - ref) / ref, rms / ref, ok ? "pass" : "FAIL"
    }
    if (!pass) exit 1
}
