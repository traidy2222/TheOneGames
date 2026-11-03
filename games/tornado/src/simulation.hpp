#pragma once
#include <theone/core/math.hpp>
#include <vector>
#include <cstdint>
namespace tornado {
struct Parameters {
    float peakWind = 68, radius = 140, maturity = 1, humidity = .72f;
};
glm::vec2 center(float height, float time);
float radiusAt(float height, const Parameters& p);
glm::vec3 wind(glm::vec3 position, float time, const Parameters& p);
float liquidWater(glm::vec3 position,float time,const Parameters& p);
struct Debris {
    glm::vec3 position{}, velocity{};
    float size=1, areaMass=0.1f, phase=0, spin=0, lift=0.15f;
    unsigned material=0; // vegetation, paper, timber, sheet metal
};
class Simulation {
    uint32_t random_ = 72849;
    float random();
    void seed(Debris& d, bool airborne);
public:
    Parameters parameters;
    float time=18;
    std::vector<Debris> debris;
    explicit Simulation(size_t count=2400);
    void reset(float atTime=18);
    void step(float dt);
};
}
