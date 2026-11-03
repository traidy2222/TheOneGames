#pragma once
#include <theone/plasma/mhd.hpp>
#include <theone/lighting/stellar.hpp>
#include <filesystem>
// Experiment/solar initial conditions belong to the game; the engine solver is generic.
class CoronaModel {
    one::PlasmaConfig config_;
    one::PlasmaGrid grid_;

  public:
    static constexpr double lengthMetres = 2e7, velocityMetresPerSecond = 2e5, densityKgPerM3 = 1e-12;
    static constexpr double timeUnit = lengthMetres / velocityMetresPerSecond;
    CoronaModel();
    void reset();
    void stressField();
    void advance(double seconds, int maximumSteps = 4);
    void saveState(const std::filesystem::path& path) const;
    double timeSeconds() const { return grid_.time() * timeUnit; }
    one::PlasmaDiagnostics diagnostics() const { return grid_.diagnostics(); }
    std::shared_ptr<const one::StellarPlasmaPatch> snapshot(const one::StellarEmitter& star) const;
};
