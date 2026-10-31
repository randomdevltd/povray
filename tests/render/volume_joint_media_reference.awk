# volume_joint_media_check(kind, eccentricity, image, output) with joint_media.awk; image and output may be empty.
# output receives "channel mean reference_mean" lines.

function self_check(    list, g, i, integral) {
    split("0 -0.4 0.4", list, " ")
    for (g = 1; g <= 3; g++) {
        integral = 0
        for (i = 0; i < 20000; i++) integral += phase(-1 + (i + 0.5) * 2 / 20000, list[g])
        integral *= 4 * JOINT_PI / 20000
        if (abs(integral - 1) >= 1e-7) fail("phase " list[g] " integrates to " integral)
    }
    for (i = 0; i < 3; i++) {
        joint_regions(i)
        integral = column_density(0, 0, 0, 0, 0, 1, i)
        if (abs(integral - (i == 2 ? 0.75 : 1)) >= 1e-12) fail("case " i " axis column " integral)
    }
    integral = segment_box_fraction(0, 0, 0, 2, 0, 0, 0.5, -1, -1, 1, 1, 1)
    if (abs(integral - 0.25) >= 1e-12) fail("segment box fraction " integral)
}

function volume_joint_media_check(kind, eccentricity, image, output,    sd, dd, fine, coarse, worst, c, convergence, n, measured, target) {
    joint_init()
    kind += 0
    eccentricity += 0
    sd = 12
    dd = 64
    self_check()
    joint_reference(0, 0, kind, eccentricity, sd, dd, fine)
    joint_reference(0, 0, kind, eccentricity, int(sd / 2), int(dd / 2), coarse)
    worst = 0
    for (c = 0; c < 3; c++) {
        convergence[c] = abs(fine[c] - coarse[c]) / fine[c]
        if (convergence[c] > worst) worst = convergence[c]
    }
    printf "case %d phase %g: centre %.9g %.9g %.9g, coarse %.9g %.9g %.9g, relative convergence %.3g %.3g %.3g (%d source, %d depth divisions)\n",
        kind, eccentricity, fine[0], fine[1], fine[2], coarse[0], coarse[1], coarse[2], convergence[0], convergence[1], convergence[2], sd, dd
    if (worst > 0.005) fail("independent source/depth integral not converged: relative " worst " > 0.005")
    if (image == "") return
    n = read_ppm(image, measured)
    joint_targets(IMAGE_W, IMAGE_H, kind, eccentricity, sd, dd, target)
    if (!joint_channels(image, "reference", measured, target, n, output)) exit 1
}
