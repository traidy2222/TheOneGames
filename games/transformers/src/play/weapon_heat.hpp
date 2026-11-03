#pragma once
#include <nlohmann/json.hpp>
namespace transformers::play {
// Heat parameters are read from the exported weapon Attribute numbers.
class WeaponHeat {
    float capacity_=0,increment_=0,delay_=0,rate_=0,overheatRate_=0,period_=0;
    float heat_=0,sinceShot_=0,lock_=0;
    bool overheated_=false;
public:
    void configure(const nlohmann::json& numbers);
    void update(float dt);
    // The round reaching capacity is accepted; subsequent rounds are refused.
    bool consume(unsigned rounds=1);
    float fraction()const;
    bool overheated()const{return overheated_;}
};
}
