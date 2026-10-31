#!/usr/bin/env python3
"""Angular references for Lambertian surfaces and isotropic volume sources.

An isotropic source's far intensity is generated_flux/(4*pi), regardless of
its shape. A convex Lambertian surface has intensity L*projected_area.
Equal total flux therefore does not imply equal angular illumination.
"""

import json
import math


def isotropic_intensity(flux):
    return flux / (4 * math.pi)


def panel_intensity(mu, width, height, radiance):
    return radiance * width * height * max(0, mu)


def box_intensity(mu, width, height, depth, radiance, azimuth=0):
    return radiance * (width * height * abs(mu)
                       + depth * (height * abs(math.cos(azimuth)) + width * abs(math.sin(azimuth)))
                       * math.sqrt(max(0, 1 - mu * mu)))


def cylinder_intensity(mu, radius, length, radiance):
    return radiance * (2 * radius * length * math.sqrt(max(0, 1 - mu * mu))
                       + math.pi * radius * radius * abs(mu))


def angular_flux(intensity, divisions=40000):
    step = 2 / divisions
    return 2 * math.pi * step * sum(intensity(-1 + (i + 0.5) * step)
                                  for i in range(divisions))


def references():
    width, height, depth = 1.2, 0.7, 0.002
    radius, length = 0.012, 1
    sphere_radius = 0.05
    shapes = [
        ("one-sided panel", math.pi * width * height,
         lambda mu: panel_intensity(mu, width, height, 1), None),
        ("closed thin box", math.pi * 2 * (width * height + width * depth + height * depth),
         lambda mu: box_intensity(mu, width, height, depth, 1),
         lambda mu: width * height * abs(mu)
         + depth * (height + width) * 2 / math.pi * math.sqrt(max(0, 1 - mu * mu))),
        ("closed cylinder", math.pi * (2 * math.pi * radius * length + 2 * math.pi * radius * radius),
         lambda mu: cylinder_intensity(mu, radius, length, 1), None),
        ("sphere", math.pi * 4 * math.pi * sphere_radius * sphere_radius,
         lambda mu: math.pi * sphere_radius * sphere_radius, None),
    ]
    result = []
    for name, flux, surface, azimuth_average in shapes:
        measured = angular_flux(azimuth_average or surface)
        assert abs(measured / flux - 1) < 1e-5, (name, measured, flux)
        volume = isotropic_intensity(flux)
        angular = []
        for degrees in [0, 30, 60, 80, 90, 120, 180]:
            mu = math.cos(math.radians(degrees))
            target = surface(mu)
            angular.append({"degrees_from_normal_or_axis": degrees,
                            "lambertian_intensity": target,
                            "equal_flux_isotropic_intensity": volume,
                            "isotropic_over_lambertian": volume / target if target > 1e-14 else None})
        result.append({"shape": name, "radiance": 1, "generated_flux": flux,
                       "angular_flux_relative_error": measured / flux - 1, "angles": angular})
    return result


if __name__ == "__main__":
    print(json.dumps(references(), indent=2))
