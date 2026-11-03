#pragma once
#include "content.hpp"
#include "physics.hpp"
#include <functional>
namespace one {
struct GameServices {
    World& world; Physics& physics;
    std::function<void()> playSound;
};
struct GameState { int collected=0,total=0; bool completed=false; };
inline const char* robotsAnimation(bool grounded,float verticalVelocity,float horizontalInput) {
    // Candidate mode/script links from the original EDB; blending is not reconstructed.
    return !grounded && verticalVelocity>0?"original_8300006f":
           !grounded?"original_83000064":horizontalInput>.05f?"original_8300007b":"original_0300000e";
}
// Edge-triggered jump budget for the Robots prototype. Holding jump never repeats it.
class DoubleJump {
    int used_=0; bool wasPressed_=false;
public:
    bool update(bool pressed,bool grounded,float verticalVelocity) {
        if(grounded && verticalVelocity<=0) used_=0;
        bool fire=pressed && !wasPressed_ && used_<2 && (grounded || used_>0);
        wasPressed_=pressed;
        if(fire) ++used_;
        return fire;
    }
    void reset() { used_=0; wasPressed_=false; }
};
// A game's module depends on engine services and content types, never Vulkan or editor internals.
class GameModule {
public:
    virtual ~GameModule()=default;
    virtual void start(GameServices&)=0;
    virtual void fixedUpdate(GameServices&,float)=0;
    virtual void interact(GameServices&)=0;
    virtual GameState state() const=0;
};
std::unique_ptr<GameModule> createSandboxGame();
}
