#pragma once
#include "arena.hpp"
#include "../player.hpp"
#include "blend.hpp"
#include "weapons.hpp"
namespace transformers::play {
struct Input {float forward=0,right=0,viewYaw=0,viewPitch=0;glm::vec3 aimOrigin{0},aimDirection{0,0,1};bool cameraAim=false,boost=false,jump=false,transform=false,attack=false,fire=false,reset=false,switchWeapon=false;};
struct Shot {glm::vec3 from,to;float life=.12f;bool hit=false;};
class Controller {
    Arena& arena_;
    void clip(const char* name,bool restart=false);
    void fire();
    void wheels(float dt);
    float actionTime_=0,fireCooldown_=0,gunTime_=0,wheelAngle_=0;
    bool attackHit_=false;
    PoseBlend blend_;
    bool requestedFire_=false;
    Input aim_;
    float takeoff_=0,landing_=0;
    bool airborne_=false;
    float chargeSeconds_=0;
    bool heldFire_=false;
public:
    Player character;
    Weapons weapons;
    std::vector<std::string> soundEvents;
    glm::vec3 position{0};
    float heading=0,speed=0,clock=0,lapTime=0,bestLap=0;
    bool vehicle=false,transforming=false,attacking=false;
    int nextCheckpoint=0,laps=0,shotsHit=0;
    std::vector<Shot> shots;
    explicit Controller(const Catalog& catalog,Arena& arena);
    void update(float dt,const Input& input);
    void reset();
};
}
