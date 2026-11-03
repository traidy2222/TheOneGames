#include "corona.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
namespace {
one::PlasmaConfig config() {
    one::PlasmaConfig c;
    c.nx = 64;
    c.ny = 96;
    c.gravity = 274 * CoronaModel::lengthMetres /
                (CoronaModel::velocityMetresPerSecond * CoronaModel::velocityMetresPerSecond);
    c.resistivity = .001;
    c.footpointShear = .008;
    return c;
}
} // namespace
CoronaModel::CoronaModel() : config_(config()), grid_(config_) {
    reset();
}
void CoronaModel::reset() {
    grid_.initialize(
        [](glm::dvec2 p) {
            constexpr double k = 3.141592653589793 / 2;
            return 3.5 / k * std::cos(k * p.x) * std::exp(-k * p.y);
        },
        [&](glm::dvec2 p) {
            one::PlasmaPrimitive q;
            double potential = config_.gravity * config_.stellarRadius * p.y / (config_.stellarRadius + p.y);
            double background = std::exp(-potential / .4);
            auto delta = (p - glm::dvec2(0, .65)) / .14;
            q.density = background * (1 + 18 * std::exp(-glm::dot(delta, delta)));
            q.pressure = background * .4;
            return q;
        });
}
void CoronaModel::stressField() {
    grid_.injectGuideField({0, .65}, .28, 4.5);
}
void CoronaModel::advance(double seconds, int maximumSteps) {
    double target = grid_.time() + seconds / timeUnit;
    for (int i = 0; i < maximumSteps && grid_.time() < target - 1e-12; ++i)
        grid_.step(target - grid_.time());
}
void CoronaModel::saveState(const std::filesystem::path& path) const {
    std::ofstream out(path);
    out << std::setprecision(12);
    out << "x_m,height_m,rho_kg_m3,pressure_pa,vx_m_s,vy_m_s,vz_m_s,bx_t,by_t,bz_t\n";
    double fieldUnit = velocityMetresPerSecond * std::sqrt(1.25663706212e-6 * densityKgPerM3);
    for (uint32_t y = 0; y < config_.ny; ++y)
        for (uint32_t x = 0; x < config_.nx; ++x) {
            auto q = grid_.cell(x, y);
            out << (config_.minimum.x + (x + .5) * config_.extent.x / config_.nx) * lengthMetres << ','
                << (config_.minimum.y + (y + .5) * config_.extent.y / config_.ny) * lengthMetres << ','
                << q.density * densityKgPerM3 << ','
                << q.pressure * densityKgPerM3 * velocityMetresPerSecond * velocityMetresPerSecond;
            for (int k = 0; k < 3; ++k)
                out << ',' << q.velocity[k] * velocityMetresPerSecond;
            for (int k = 0; k < 3; ++k)
                out << ',' << q.magnetic[k] * fieldUnit;
            out << '\n';
        }
    if (!out)
        throw std::runtime_error("Cannot save MHD state");
}
std::shared_ptr<const one::StellarPlasmaPatch> CoronaModel::snapshot(const one::StellarEmitter& star) const {
    auto patch = std::make_shared<one::StellarPlasmaPatch>();
    auto r = one::stellarActivityRegion(star, 0);
    patch->normal = glm::vec3(r.normal);
    patch->tangent = glm::vec3(r.tangent);
    patch->lengthMetres = lengthMetres;
    patch->halfDepthMetres = 8e6;
    patch->resolution = {config_.nx, config_.ny};
    patch->minimum = glm::vec2(config_.minimum);
    patch->extent = glm::vec2(config_.extent);
    patch->cells.reserve(size_t(config_.nx) * config_.ny);
    constexpr double proton = 1.67262192369e-27, boltzmann = 1.380649e-23;
    for (uint32_t y = 0; y < config_.ny; ++y)
        for (uint32_t x = 0; x < config_.nx; ++x) {
            auto q = grid_.cell(x, y);
            double ne = q.density * densityKgPerM3 / proton;
            double kelvin = q.pressure / q.density * proton * velocityMetresPerSecond *
                            velocityMetresPerSecond / (2 * boltzmann);
            double height = config_.minimum.y + (y + .5) * config_.extent.y / config_.ny;
            double potential =
                config_.gravity * config_.stellarRadius * height / (config_.stellarRadius + height);
            double ambient = densityKgPerM3 / proton * std::exp(-potential / .4);
            double ambientT =
                .4 * proton * velocityMetresPerSecond * velocityMetresPerSecond / (2 * boltzmann);
            patch->cells.emplace_back(float(ne), float(kelvin), float(ambient), float(ambientT));
        }
    return patch;
}
