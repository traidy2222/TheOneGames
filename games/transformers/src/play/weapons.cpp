#include "weapons.hpp"
#include <algorithm>
namespace transformers::play {
Weapons::Weapons(Player& player):player_(player) {
    const auto& definitions=player.catalog().data.at("weapons");
    for(const auto& w:definitions) {
        const auto name=stringOr(w,"weapon");int slot=-1;
        if(name=="PrimeLaserRifle")slot=0;
        else if(name=="PrimeLaserRifleLeftHand")slot=1;
        else if(name=="OptimusVehicleWeapon")slot=2;
        if(slot>=0)pieces_[slot]=player.catalog().piece(w.at("file"));
    }
    for(int mode=0;mode<2;++mode){shell_=mode;for(int slot=0;slot<3;++slot)heat_[slot+mode*3].configure(definition(slot).at("numbers"));}shell_=false;
}
const Json& Weapons::definition(int slot)const {
    const char* name=slot==2?"OptimusVehicleWeapon":slot==1?(shell_?"ShellCannonL":"PrimeLaserRifleLeftHand"):(shell_?"ShellCannon":"PrimeLaserRifle");
    for(const auto& w:player_.catalog().data.at("weapons"))if(w.at("weapon")==name)return w;
    throw std::runtime_error("Missing native weapon definition");
}
float Weapons::cooldown(bool vehicle)const{return definition(vehicle?2:0).at("numbers").value("rateOfFire",.1f);}
void Weapons::pose(int slot) {
    const int p=pieces_[slot];if(p<0)return;
    const auto& piece=player_.catalog().pieces[p];const auto& w=definition(slot);
    auto& track=player_.tracks[p];track.reset(piece);
    track.visible=equipped_&&(vehicle_?slot==2:slot<2);if(!track.visible){player_.poses[p].clear();return;}
    track.attachment=stringOr(w,"start_node");
    const std::string idle=slot==2?"":shell_?"Idle_WeaponB":"Idle_WeaponA";
    track.play(idle,0,false);
    const std::string fire=shell_?"FireB":"FireA";
    const bool hot=heat_[slot+(!vehicle_&&shell_?3:0)].overheated();
    const auto overheat=shell_?"Overheat_WeaponB":"Overheat_WeaponA";
    if(hot&&piece.animation(overheat)>=0)track.play(overheat,0,true);
    const int animation=piece.animation(fire);
    if(!hot&&slot<2&&animation>=0) {
        const float duration=piece.model->animations[animation].duration;
        if(sinceShot_[slot]<duration)track.play(fire,0,true);
    }
    player_.poses[p]=track.pose(sinceShot_[slot]);
}
void Weapons::prepare(bool vehicle,bool equipped,float dt) {
    vehicle_=vehicle;equipped_=equipped;
    for(auto& heat:heat_)heat.update(dt);
    for(int i=0;i<3;++i){sinceShot_[i]+=dt;pose(i);}
}
std::vector<glm::vec3> Weapons::muzzles(bool vehicle)const {
    std::vector<glm::vec3> result;
    for(int slot=vehicle?2:0;slot<(vehicle?3:2);++slot) {
        const int p=pieces_[slot];if(p<0||!player_.tracks[p].visible)continue;
        const auto placement=player_.placement(p);if(!placement)continue;
        const auto& piece=player_.catalog().pieces[p];
        for(const auto& barrel:definition(slot).at("barrel_nodes")) {
            const int node=piece.node(barrel.get<std::string>());
            if(node>=0&&size_t(node)<player_.poses[p].size())result.push_back(glm::vec3(*placement*player_.poses[p][node]*glm::vec4(0,0,0,1)));
        }
    }
    return result;
}
std::vector<WeaponShot> Weapons::fire(bool vehicle,float chargeSeconds) {
    std::vector<WeaponShot> shots;if(!equipped_||vehicle!=vehicle_)return shots;
    // Laser pairs alternate; the shell pair is authored with alternatingFire=false.
    const int first=vehicle?2:shell_?0:int(nextHand_++%2);
    const int count=!vehicle&&shell_?2:1;
    for(int slot=first;slot<first+count;++slot) {
        const int p=pieces_[slot];if(p<0)continue;
        if(!heat_[slot+(!vehicle&&shell_?3:0)].consume())continue;
        sinceShot_[slot]=0;pose(slot);
        const auto placement=player_.placement(p);if(!placement)continue;
        const auto& w=definition(slot);const auto& piece=player_.catalog().pieces[p];
        for(const auto& barrel:w.at("barrel_nodes")) {
            const int node=piece.node(barrel.get<std::string>());if(node<0)continue;
            const auto position=glm::vec3(*placement*player_.poses[p][node]*glm::vec4(0,0,0,1));
            const auto& numbers=w.at("numbers");
            const float charge=!vehicle&&shell_?std::clamp((chargeSeconds-numbers.value("timeTilChargeStarts",.1f))/numbers.value("chargeDurationTilMaxEffect",2.25f),0.f,1.f):0.f;
            const float multiplier=glm::mix(numbers.value("minChargeDamageMultiplier",1.f),numbers.value("maxChargeDamageMultiplier",1.f),charge);
            shots.push_back({position,stringOr(w.at("sounds"),"fire"),numbers.value("bulletRange",100.f),numbers.value("damage",12.f)*multiplier});
        }
    }
    return shots;
}
}
