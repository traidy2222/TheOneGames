#pragma once
#include <theone/physics/physics.hpp>
#include <optional>
namespace transformers::play {
// The existing capsule is 1.6 units tall; this adapter presents a nine-metre robot.
inline constexpr float metresPerUnit=5.625f, centreHeight=4.5f;
struct Target { uint64_t id; int hits=0; };
struct Hit { uint64_t id; glm::vec3 point; float distance; };
class Arena {
    std::vector<std::shared_ptr<one::Model>> materials_;
    uint64_t nextId_=1000;
    uint64_t box(glm::vec3 position,glm::vec3 size,int material,bool solid=true,bool dynamic=false,float slope=0);
public:
    one::World world,collision;
    one::Physics physics;
    std::vector<Target> targets;
    std::vector<glm::vec3> checkpoints;
    glm::vec3 spawn{0,0,-65};
    Arena();
    void step(float dt,glm::vec3 velocity,bool jump);
    void resetPlayer();
    glm::vec3 position() const;
    std::optional<Hit> ray(glm::vec3 origin,glm::vec3 direction,float range) const;
    bool hit(uint64_t id,glm::vec3 direction,float strength);
    int completedTargets() const;
};
void appendScene(one::RenderScene& destination,const one::RenderScene& source);
}
