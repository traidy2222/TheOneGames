#pragma once
#include <theone/render/renderer.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
// Optional instrumentation of the real visible application, not the smoke-test path.
class VisibleProfile {
    using Clock = std::chrono::steady_clock;
    std::ofstream output;
    std::map<std::string, std::vector<double>> samples;
    Clock::time_point started, previous;
    int step_ = -1, limit_;
    std::string phase_ = "startup";
    double inputHostMs = 0, pollHostMs = 0;

  public:
    explicit VisibleProfile(const std::filesystem::path& path, int frames, bool probe = false);
    bool enabled() const { return output.is_open(); }
    int step() const { return step_; }
    void begin(bool ready);
    double mark();
    void sample(const std::string& name, double ms);
    void inputSampled();
    void pollInput();
    void end(const one::FrameStats& stats, size_t textureBytes, bool busy, bool visible);
    bool done() const { return enabled() && step_ >= limit_; }
    void finish();
};
