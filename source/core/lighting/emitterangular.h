#ifndef POVRAY_CORE_EMITTERANGULAR_H
#define POVRAY_CORE_EMITTERANGULAR_H

#include "core/configcore.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/math/vector.h"

namespace pov
{

class EmitterAngularField final
{
    public:
        template<class Area>
        bool Build(int latitudeIntervals, int longitudeIntervals, Area area)
        {
            nphi = longitudeIntervals;
            mu.resize(latitudeIntervals + 1);
            values.resize((latitudeIntervals + 1) * nphi);
            for (int i = 0; i <= latitudeIntervals; i++)
            {
                mu[i] = -std::cos(M_PI * i / latitudeIntervals);
                const double radial = std::sqrt(std::max(0.0, 1.0 - mu[i] * mu[i]));
                for (int j = 0; j < nphi; j++)
                {
                    const double phi = 2.0 * M_PI * j / nphi;
                    double a = ((i == 0 || i == latitudeIntervals) && j > 0) ? values[i * nphi] :
                               area(Vector3d(radial * std::cos(phi), radial * std::sin(phi), mu[i]));
                    if (!(a >= 0.0) || !std::isfinite(a))
                        return false;
                    values[i * nphi + j] = a;
                }
            }
            mean = 0.0;
            for (int i = 0; i < latitudeIntervals; i++)
                for (int j = 0; j < nphi; j++)
                    mean += (mu[i + 1] - mu[i]) * (values[i * nphi + j] + values[(i + 1) * nphi + j]) / (4.0 * nphi);
            return mean > 0.0;
        }

        double MeanArea() const { return mean; }

        double Area(const Vector3d& direction) const
        {
            const double z = std::min(1.0, std::max(-1.0, direction[Z]));
            const int i = std::min(int(mu.size()) - 2, std::max(0, int(std::upper_bound(mu.begin(), mu.end(), z) - mu.begin()) - 1));
            const double t = (z - mu[i]) / (mu[i + 1] - mu[i]);
            double phi = std::atan2(direction[Y], direction[X]);
            if (phi < 0.0)
                phi += 2.0 * M_PI;
            const double column = phi * nphi / (2.0 * M_PI);
            const int j = std::min(nphi - 1, int(column)), next = (j + 1) % nphi;
            const double u = column - j;
            const double a = values[i * nphi + j] * (1.0 - u) + values[i * nphi + next] * u;
            const double b = values[(i + 1) * nphi + j] * (1.0 - u) + values[(i + 1) * nphi + next] * u;
            return a * (1.0 - t) + b * t;
        }

        double Weight(const Vector3d& direction) const { return Area(direction) / mean; }

    private:
        int nphi = 0;
        std::vector<double> mu, values;
        double mean = 0.0;
};

}

#endif
