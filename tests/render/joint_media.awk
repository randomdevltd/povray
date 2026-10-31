# Cartesian finite-source radiative transfer through piecewise constant voxels (needs image.awk).
# JOINT_MODEL selects the optical field: empty for volume_joint_media, "roles" or "mixed".

function joint_init() {
    JOINT_PI = atan2(0, -1)
    JOINT_ABSORPTION[0] = 0.2; JOINT_ABSORPTION[1] = 0.4; JOINT_ABSORPTION[2] = 0.6
    JOINT_SCATTERING[0] = 0.04; JOINT_SCATTERING[1] = 0.03; JOINT_SCATTERING[2] = 0.02
    JOINT_COLOR[0] = 1; JOINT_COLOR[1] = 0.5; JOINT_COLOR[2] = 0.2
}

function joint_region(a, b, density) {
    JOINT_LOW[JOINT_REGIONS] = a
    JOINT_HIGH[JOINT_REGIONS] = b
    JOINT_DENSITY[JOINT_REGIONS++] = density
}

# Sets JOINT_LOW, JOINT_HIGH and JOINT_DENSITY for the case; returns the region count.
function joint_regions(kind) {
    JOINT_REGIONS = 0
    if (JOINT_MODEL == "mixed") {
        joint_region(0, 0.25, 1); joint_region(0.25, 0.75, 1.5); joint_region(0.75, 1, 1)
    } else if (JOINT_MODEL == "roles") {
        if (kind == 1 || kind == 2) {
            joint_region(0, 0.25, 1); joint_region(0.25, 0.75, kind == 1 ? 0.5 : 0.75); joint_region(0.75, 1, 1)
        } else
            joint_region(0, 1, 1)
    } else if (kind == 1 || kind == 4) {
        joint_region(0, 0.5, 0.5); joint_region(0.5, 1, 1.5)
    } else if (kind == 2) {
        joint_region(0, 0.25, 1); joint_region(0.25, 0.75, 0.5); joint_region(0.75, 1, 1)
    } else
        joint_region(0, 1, 1)
    return JOINT_REGIONS
}

function joint_axis(a, b, lo, hi,    delta, first, last, swap) {
    delta = b - a
    if (delta == 0) return lo <= a && a <= hi
    first = (lo - a) / delta
    last = (hi - a) / delta
    if (first > last) { swap = first; first = last; last = swap }
    if (first > JOINT_ENTER) JOINT_ENTER = first
    if (last < JOINT_LEAVE) JOINT_LEAVE = last
    return 1
}

function segment_box_fraction(ox, oy, oz, tx, ty, tz, lx, ly, lz, hx, hy, hz) {
    JOINT_ENTER = 0
    JOINT_LEAVE = 1
    if (!joint_axis(ox, tx, lx, hx) || !joint_axis(oy, ty, ly, hy) || !joint_axis(oz, tz, lz, hz)) return 0
    return max2(0, JOINT_LEAVE - JOINT_ENTER)
}

# Uses the regions of the last joint_regions call; they share the x and y slabs, so those are clipped once.
function column_density(ox, oy, oz, tx, ty, tz, kind,    dx, dy, dz, span, column, r, enter, leave, first, last, swap, lo, hi) {
    dx = tx - ox; dy = ty - oy; dz = tz - oz
    span = sqrt(dx * dx + dy * dy + dz * dz)
    if (JOINT_MODEL == "mixed")
        return span * (segment_box_fraction(ox, oy, oz, tx, ty, tz, -0.1, -0.1, 0, 0.1, 0.1, 1) \
            + 0.5 * segment_box_fraction(ox, oy, oz, tx, ty, tz, -0.09, -0.09, 0.25, 0.09, 0.09, 0.75))
    JOINT_ENTER = 0
    JOINT_LEAVE = 1
    if (!joint_axis(ox, tx, -0.1, 0.1) || !joint_axis(oy, ty, -0.1, 0.1)) return 0
    column = 0
    for (r = 0; r < JOINT_REGIONS; r++) {
        lo = JOINT_LOW[r]; hi = JOINT_HIGH[r]
        enter = JOINT_ENTER; leave = JOINT_LEAVE
        if (dz == 0) {
            if (lo > oz || oz > hi) continue
        } else {
            first = (lo - oz) / dz
            last = (hi - oz) / dz
            if (first > last) { swap = first; first = last; last = swap }
            if (first > enter) enter = first
            if (last < leave) leave = last
        }
        if (leave > enter) column += JOINT_DENSITY[r] * (leave - enter)
    }
    return span * column
}

function phase(cosine, eccentricity) {
    return (1 - eccentricity ^ 2) / (4 * JOINT_PI * (1 + eccentricity ^ 2 - 2 * eccentricity * cosine) ^ 1.5)
}

# Fills result[0..2] with the radiance seen along the camera ray at (x, y).
function joint_reference(x, y, kind, eccentricity, source_divisions, depth_divisions, result,
                         count, i, j, k, luminance, emitted, regions, r, low, high, density, step, slot,
                         pz, camera, s, dx, dy, dz, distance_squared, distance, weight, optical,
                         s0, s1, s2, c0, c1, c2, x0, x1, x2, r0, r1, r2, numerator, spread, twice) {
    count = 0
    for (i = 0; i < source_divisions; i++)
        for (j = 0; j < source_divisions; j++)
            for (k = 0; k < source_divisions; k++) {
                JOINT_SX[count] = 1 + 0.2 * (i + 0.5) / source_divisions
                JOINT_SY[count] = -0.1 + 0.2 * (j + 0.5) / source_divisions
                JOINT_SZ[count++] = 0.35 + 0.3 * (k + 0.5) / source_divisions
            }
    luminance = JOINT_COLOR[0] * 0.297 + JOINT_COLOR[1] * 0.589 + JOINT_COLOR[2] * 0.114
    emitted = 100 / (4 * JOINT_PI * luminance * count)
    s0 = JOINT_SCATTERING[0]; s1 = JOINT_SCATTERING[1]; s2 = JOINT_SCATTERING[2]
    c0 = JOINT_COLOR[0]; c1 = JOINT_COLOR[1]; c2 = JOINT_COLOR[2]
    x0 = JOINT_ABSORPTION[0] + s0; x1 = JOINT_ABSORPTION[1] + s1; x2 = JOINT_ABSORPTION[2] + s2
    r0 = r1 = r2 = 0
    numerator = 1 - eccentricity ^ 2
    spread = 1 + eccentricity ^ 2
    twice = 2 * eccentricity
    regions = joint_regions(kind)
    for (r = 0; r < regions; r++) {
        low = JOINT_LOW[r]; high = JOINT_HIGH[r]; density = JOINT_DENSITY[r]
        step = (high - low) / depth_divisions
        for (slot = 0; slot < depth_divisions; slot++) {
            pz = low + (slot + 0.5) * step
            camera = column_density(x, y, 0, x, y, pz, kind)
            for (s = 0; s < count; s++) {
                if (kind == 3 && segment_box_fraction(x, y, pz, JOINT_SX[s], JOINT_SY[s], JOINT_SZ[s], 0.49, -1, -1, 0.51, 1, 0.5) > 0) continue
                dx = JOINT_SX[s] - x; dy = JOINT_SY[s] - y; dz = JOINT_SZ[s] - pz
                distance_squared = dx * dx + dy * dy + dz * dz
                distance = sqrt(distance_squared)
                weight = step * density * emitted * (numerator / (4 * JOINT_PI * (spread - twice * (dz / distance)) ^ 1.5)) / distance_squared
                optical = camera + column_density(x, y, pz, JOINT_SX[s], JOINT_SY[s], JOINT_SZ[s], kind)
                r0 += weight * s0 * c0 * exp(-x0 * optical)
                r1 += weight * s1 * c1 * exp(-x1 * optical)
                r2 += weight * s2 * c2 * exp(-x2 * optical)
            }
        }
    }
    result[0] = r0; result[1] = r1; result[2] = r2
}

# Fills target[] with the reference of every pixel of a width x height frame 0.02 units across.
function joint_targets(width, height, kind, eccentricity, source_divisions, depth_divisions, target,    row, col, rgb, k) {
    k = 0
    for (row = 0; row < height; row++)
        for (col = 0; col < width; col++) {
            joint_reference(0.02 * ((col + 0.5) / width - 0.5), 0.02 * (0.5 - (row + 0.5) / height), kind, eccentricity,
                            source_divisions, depth_divisions, rgb)
            target[k++] = rgb[0]; target[k++] = rgb[1]; target[k++] = rgb[2]
        }
    return k
}

# Prints and returns the per-channel mean agreement of measured against target; limit 3% of the reference mean.
function joint_channels(label, name, measured, target, n, output,    c, i, got_mean, want_mean, rms, limit, pass, all) {
    all = 1
    for (c = 0; c < 3; c++) {
        got_mean = want_mean = rms = 0
        for (i = c; i < n; i += 3) {
            got_mean += measured[i]; want_mean += target[i]; rms += (measured[i] - target[i]) ^ 2
        }
        got_mean /= n / 3; want_mean /= n / 3
        rms = sqrt(rms / (n / 3))
        limit = max2(2 / 65535, 0.03 * want_mean)
        pass = abs(got_mean - want_mean) <= limit
        all = all && pass
        printf "%s channel %d: mean %.9g, %s %.9g, limit %.9g, relative bias %.6g, relative pixel rms %.6g, %s\n",
            label, c, got_mean, name, want_mean, limit, (got_mean - want_mean) / want_mean, rms / want_mean, pass ? "pass" : "FAIL"
        if (output != "") printf "%d %.17g %.17g\n", c, got_mean, want_mean > output
    }
    if (output != "") close(output)
    return all
}
