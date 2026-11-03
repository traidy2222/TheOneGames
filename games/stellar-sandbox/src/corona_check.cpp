#include "corona.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        CoronaModel model;
        auto initial = model.diagnostics();
        model.stressField();
        auto stressed = model.diagnostics();
        if (stressed.injectedEnergy <= 0)
            throw std::runtime_error("No magnetic work injected");
        auto start = std::chrono::steady_clock::now();
        model.advance(180, 10000);
        auto d = model.diagnostics();
        auto patch = model.snapshot(one::StellarEmitter{});
        if (model.timeSeconds() < 179.9 || d.relativeDivergence > 1e-11 || d.minimumDensity <= 0 ||
            d.minimumPressure <= 0 || d.rejectedSteps > 0)
            throw std::runtime_error("Coronal MHD invariant failed");
        if (d.kineticEnergy < .001)
            throw std::runtime_error("Magnetic stress did not accelerate plasma");
        CoronaModel quiet;
        quiet.advance(180, 10000);
        auto control = quiet.diagnostics();
        if (d.kineticEnergy < control.kineticEnergy * 1.5)
            throw std::runtime_error("Magnetic work had no dynamical effect over gravity-only control");
        double minimumT = 1e20, maximumT = 0;
        for (auto q : patch->cells) {
            minimumT = std::min(minimumT, double(q.y));
            maximumT = std::max(maximumT, double(q.y));
        }
        std::cout << "PASS coronal MHD seconds=" << model.timeSeconds() << " steps=" << d.steps
                  << " divB=" << d.relativeDivergence << " kinetic=" << d.kineticEnergy
                  << " injected=" << d.injectedEnergy << " control_kinetic=" << control.kineticEnergy
                  << " mass_ratio=" << d.mass / initial.mass << " temperature=" << minimumT << ".."
                  << maximumT << " wall_s="
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
