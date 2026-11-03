#include "weapon_heat.hpp"
#include <algorithm>
#include <cmath>
namespace transformers::play {
void WeaponHeat::configure(const nlohmann::json& numbers) {
    auto number=[&](const char* key,float fallback=0.f) {
        const auto i=numbers.find(key);
        if(i==numbers.end()||!i->is_number())return fallback;
        const float value=i->get<float>();return std::isfinite(value)?value:fallback;
    };
    capacity_=std::max(0.f,number("heatMeterCapacity"));
    increment_=std::max(0.f,number("heatMeterIncrementPerRound"));
    delay_=std::max(0.f,number("heatMeterCooldownTilDecrement"));
    rate_=std::max(0.f,number("heatMeterDecrementPerSecond"));
    period_=std::max(0.f,number("heatMeterOverHeatPeriod"));
    overheatRate_=number("heatMeterOverHeatCooldownRate",-1.f);
    if(overheatRate_<0)overheatRate_=rate_;
    heat_=sinceShot_=lock_=0;overheated_=false;
}
void WeaponHeat::update(float dt) {
    if(!std::isfinite(dt)||dt<=0)return;
    // Integrate only the part of this step following the cooldown delay.
    const float before=std::max(0.f,sinceShot_-delay_);
    sinceShot_+=dt;
    const float cooling=std::max(0.f,sinceShot_-delay_)-before;
    heat_=std::max(0.f,heat_-cooling*(overheated_?overheatRate_:rate_));
    lock_=std::max(0.f,lock_-dt);
    // A hot weapon remains locked for the source period and until cooled.
    if(overheated_&&lock_<=1e-5f&&heat_<=1e-5f){overheated_=false;heat_=0;lock_=0;}
}
bool WeaponHeat::consume(unsigned rounds) {
    if(!rounds)return true;
    if(overheated_)return false;
    if(capacity_<=0)return true;
    heat_=std::min(capacity_,heat_+increment_*float(rounds));sinceShot_=0;
    if(heat_>=capacity_){overheated_=true;lock_=period_;}
    return true;
}
float WeaponHeat::fraction()const{return capacity_>0?std::clamp(heat_/capacity_,0.f,1.f):0.f;}
}
