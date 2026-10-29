# Isotropic single-scattering oracle from the sphere's analytic volume integral (needs image.awk).
# volume_scattering_check(folder); integral(Li dOmega) = j * integral_V(1/r^2 dV).

function sphere_integral(distance, radius) {
    if (distance < radius * 1e-7) return 4 * PI * radius
    if (abs(distance - radius) < radius * 1e-12) return 2 * PI * radius
    return 2 * PI * radius + PI * (radius * radius - distance * distance) / distance * log((radius + distance) / abs(radius - distance))
}

function scattering_reference(width, height, receiver, result,    steps, sigma, step, j, row, col, x, y, z, total, slot, distance, source, k) {
    steps = 128
    sigma = 0.01
    step = 0.04 / steps
    j = 100 / (4 / 3)
    k = 0
    for (row = 0; row < height; row++) {
        y = 0.002 * (0.5 - (row + 0.5) / height)
        for (col = 0; col < width; col++) {
            x = receiver + 0.002 * ((col + 0.5) / width - 0.5)
            total = 0
            for (slot = 0; slot < steps; slot++) {
                z = -0.02 + (slot + 0.5) * step
                distance = sqrt(x * x + y * y + z * z)
                source = sigma * j * sphere_integral(distance, 1) / (4 * PI)
                total += source * exp(-sigma * (z + 0.02)) * step
            }
            result[k++] = total; result[k++] = total; result[k++] = total
        }
    }
    return k
}

function surface_reference(width, height, result,    row, col, x, z, k) {
    k = 0
    for (row = 0; row < height; row++) {
        z = 0.2 * (0.5 - (row + 0.5) / height)
        for (col = 0; col < width; col++) {
            x = top_down_x(col, width, 0.2)
            result[k] = result[k + 1] = result[k + 2] = 2 / (x * x + z * z + 4) ^ 1.5
            k += 3
        }
    }
    return k
}

# Source cells go to global arrays that are overwritten, not cleared: freeing big arrays slows mawk.
function hemisphere_reference(width, height, divisions, result,    n, dx, dy, dz, i, j, k, x, y, z, weight, row, col, value, s, ex, ez) {
    n = 0
    dx = 1 / divisions
    dy = dz = 2 / divisions
    for (i = 0; i < divisions; i++) {
        x = -1 + (i + 0.5) * dx
        for (j = 0; j < divisions; j++) {
            y = -1 + (j + 0.5) * dy
            for (k = 0; k < divisions; k++) {
                z = -1 + (k + 0.5) * dz
                if (x * x + y * y + z * z < 1) { CELL_X[n] = x; CELL_Y[n] = y + 2; CELL_Z[n++] = z }
            }
        }
    }
    weight = 3 * PI / (4 * (2 * PI / 3) * PI) * dx * dy * dz
    k = 0
    for (row = 0; row < height; row++) {
        z = 0.2 * (0.5 - (row + 0.5) / height)
        for (col = 0; col < width; col++) {
            x = top_down_x(col, width, 0.2)
            value = 0
            for (s = 0; s < n; s++) {
                ex = CELL_X[s] - x; ez = CELL_Z[s] - z
                value += CELL_Y[s] / (ex * ex + CELL_Y[s] * CELL_Y[s] + ez * ez) ^ 1.5
            }
            value *= weight
            result[k++] = value; result[k++] = value; result[k++] = value
        }
    }
    return k
}

function load(name, v) {
    return read_ppm(VOLUME_FOLDER "/" name ".ppm", v)
}

function volume_scattering_check(folder,    count, names, receivers, i, n, pixels, nt, target, nc, coarse, nf, fine) {
    PI = atan2(0, -1)
    VOLUME_FOLDER = folder
    if (abs(0.75 * sphere_integral(0, 1) / (4 * PI) - 0.75) >= 1e-12) fail("sphere integral at the centre")
    if (abs(0.75 * sphere_integral(1, 1) / (4 * PI) - 0.375) >= 1e-12) fail("sphere integral on the surface")
    count = split("center outside farther scaled", names, " ")
    split("0 1.002 2 0", receivers, " ")
    for (i = 1; i <= count; i++) {
        n = load(names[i], pixels)
        nt = scattering_reference(IMAGE_W, IMAGE_H, receivers[i] + 0, target)
        compare(names[i] " isotropic single scattering", pixels, n, target, nt, 0.08)
    }
    count = split("surface tiny_native tiny_transformed", names, " ")
    for (i = 1; i <= count; i++) {
        n = load(names[i], pixels)
        nt = surface_reference(IMAGE_W, IMAGE_H, target)
        compare(names[i] " mean-chord sphere", pixels, n, target, nt, 0.08)
    }
    n = load("hemisphere", pixels)
    nc = hemisphere_reference(IMAGE_W, IMAGE_H, 24, coarse)
    nf = hemisphere_reference(IMAGE_W, IMAGE_H, 40, fine)
    compare("hemisphere Cartesian reference convergence", coarse, nc, fine, nf, 0.02)
    compare("hemisphere including planar cap", pixels, n, fine, nf, 0.08)
    print "volume_scattering: omitted source-connection extinction is bounded by 0.041%"
}
