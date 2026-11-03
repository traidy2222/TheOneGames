#pragma once
#include "../player.hpp"
#include "weapon_heat.hpp"
#include <array>
#include <algorithm>
namespace transformers::play {
struct WeaponShot {glm::vec3 origin;std::string sound;float range=100,damage=12;};
// Native weapon data chooses mounts, barrels, object animations and firing parameters.
class Weapons {
    Player& player_;
    std::array<int,3> pieces_{{-1,-1,-1}};
    std::array<float,3> sinceShot_{{100,100,100}};
    bool shell_=false,vehicle_=false,equipped_=false;
    unsigned nextHand_=0;
    std::array<WeaponHeat,6> heat_;
    const Json& definition(int slot)const;
    void pose(int slot);
public:
    explicit Weapons(Player& player);
    void prepare(bool vehicle,bool equipped,float dt);
    std::vector<WeaponShot> fire(bool vehicle,float chargeSeconds=0);
    void setShell(bool value){shell_=value;}
    bool shell()const{return shell_;}
    float cooldown(bool vehicle)const;
    float heat()const{return std::max(heat_[vehicle_?2:shell_?3:0].fraction(),vehicle_?0.f:heat_[shell_?4:1].fraction());}
    std::vector<glm::vec3> muzzles(bool vehicle)const;
};
}
