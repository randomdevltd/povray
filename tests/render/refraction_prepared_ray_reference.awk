# Exact Fermat invariant for n(x)=1.1+0.1x with planar entry and exit in air (needs image.awk).

# Sets RAY_SCREEN, RAY_INSIDE and RAY_EXIT_SINE for the ray entering at x; without bend it goes straight.
function ray(x, bend,    k, entry, a, ea, cosh, sinh, exit_x) {
    if (!bend) { RAY_SCREEN = x; RAY_INSIDE = 1; RAY_EXIT_SINE = 0; return }
    k = .1
    entry = 1.1 + k * x
    a = k / entry
    ea = exp(a)
    cosh = (ea + 1 / ea) / 2
    sinh = (ea - 1 / ea) / 2
    exit_x = x + entry / k * (cosh - 1)
    RAY_EXIT_SINE = entry * sinh
    if (abs(RAY_EXIT_SINE) >= 1) fail("ray at " x " is totally reflected")
    RAY_SCREEN = exit_x + RAY_EXIT_SINE / sqrt(1 - RAY_EXIT_SINE ^ 2)
    RAY_INSIDE = entry / k * sinh
}

# Largest difference between the screen coordinate coded as (red/green - .5)/.2 and the traced one.
function screen_error(pixels, width, height, bend,    i, worst, error) {
    worst = 0
    for (i = 0; i < width * height; i++) {
        ray(.1 * ((i % width + .5) / width - .5), bend)
        if (pixels[3 * i + 1] <= 0) fail("coordinate signal vanished at pixel " i)
        error = abs((pixels[3 * i] / pixels[3 * i + 1] - .5) / .2 - RAY_SCREEN)
        if (error > worst) worst = error
    }
    return worst
}

function refraction_prepared_ray_check(directory,    names, i, w, h, error, passed, linear, absorbed, frame, worst, positions, x0, target, ratio, position) {
    split("constant linear absorbed native", names, " ")
    read_ppm(directory "/linear.ppm", linear); w = IMAGE_W; h = IMAGE_H
    read_ppm(directory "/absorbed.ppm", absorbed)
    passed = 1
    for (i = 1; i <= 4; i++) {
        read_ppm(directory "/" names[i] ".ppm", frame)
        if (IMAGE_W != w || IMAGE_H != h) fail(names[i] ": " IMAGE_W "x" IMAGE_H " differs from linear " w "x" h)
        error = screen_error(frame, w, h, names[i] != "constant")
        printf "%s: maximum screen coordinate error %.6g (limit .001) %s\n", names[i], error, error <= .001 ? "pass" : "FAIL"
        passed = passed && error <= .001
    }
    worst = positions = 0
    for (i = 0; i < w * h; i++) {
        x0 = .1 * ((i % w + .5) / w - .5)
        ray(x0, 1)
        target = exp(-.7 * RAY_INSIDE)
        ratio = absorbed[3 * i + 1] / linear[3 * i + 1]
        if (abs(ratio - target) / target > worst) worst = abs(ratio - target) / target
        position = abs((absorbed[3 * i] / absorbed[3 * i + 1] - linear[3 * i] / linear[3 * i + 1]) / .2)
        if (position > positions) positions = position
    }
    printf "gray Beer: maximum relative error %.6g (limit .03) %s\n", worst, worst <= .03 ? "pass" : "FAIL"
    printf "sigma geometry independence: maximum screen coordinate difference %.6g (limit .001) %s\n", positions, positions <= .001 ? "pass" : "FAIL"
    if (!(passed && worst <= .03 && positions <= .001)) exit 1
}
