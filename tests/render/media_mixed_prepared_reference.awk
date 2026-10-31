# media_mixed_prepared_check(image) with joint_media.awk: two overlapping optical boxes.

function media_mixed_prepared_check(image,    fine, coarse, worst, c, n, measured, target) {
    joint_init()
    JOINT_MODEL = "mixed"
    joint_reference(0, 0, 0, 0, 12, 64, fine)
    joint_reference(0, 0, 0, 0, 6, 32, coarse)
    worst = 0
    for (c = 0; c < 3; c++) if (abs(fine[c] - coarse[c]) / fine[c] > worst) worst = abs(fine[c] - coarse[c]) / fine[c]
    printf "centre %.9g %.9g %.9g, relative reference convergence %.3g\n", fine[0], fine[1], fine[2], worst
    if (worst > 0.005) fail("independent optical/source quadrature has not converged: relative " worst " > 0.005")
    n = read_ppm(image, measured)
    joint_targets(IMAGE_W, IMAGE_H, 0, 0, 12, 64, target)
    if (!joint_channels(image, "reference", measured, target, n, "")) exit 1
}
