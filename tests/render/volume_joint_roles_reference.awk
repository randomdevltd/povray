# volume_joint_roles_check(kind, image, control) with joint_media.awk; case 3 compares the image with its
# matched explicit-density control instead of the voxel integral.

function volume_joint_roles_check(kind, image, control,    n, measured, w, h, target, fine, coarse, worst, c) {
    joint_init()
    JOINT_MODEL = "roles"
    kind += 0
    if (kind == 3) {
        if (image == "" || control == "")
            fail("independent index field requires matched explicit-density control with identical index boundaries")
        n = read_ppm(image, measured); w = IMAGE_W; h = IMAGE_H
        read_ppm(control, target)
        if (w != IMAGE_W || h != IMAGE_H) fail("control dimensions differ")
        print "case 3: matched explicit-density control, same nonzero refraction"
        if (!joint_channels(image, "control mean", measured, target, n, "")) exit 1
        return
    }
    joint_reference(0, 0, kind, 0, 12, 64, fine)
    joint_reference(0, 0, kind, 0, 6, 32, coarse)
    worst = 0
    for (c = 0; c < 3; c++) if (abs(fine[c] - coarse[c]) / fine[c] > worst) worst = abs(fine[c] - coarse[c]) / fine[c]
    printf "case %d: centre %.9g %.9g %.9g, relative convergence %.3g\n", kind, fine[0], fine[1], fine[2], worst
    if (worst > 0.005) fail("independent voxel quadrature has not converged: relative " worst " > 0.005")
    if (image == "") return
    n = read_ppm(image, measured)
    joint_targets(IMAGE_W, IMAGE_H, kind, 0, 12, 64, target)
    if (!joint_channels(image, "reference", measured, target, n, "")) exit 1
}
