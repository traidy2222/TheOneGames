#include "player.hpp"
#include <algorithm>
#include <stdexcept>

namespace transformers {
Player::Player(const Catalog& catalog) : catalog_(catalog), tracks(catalog.pieces.size()) {
    select(catalog_.clip("IdleStandSet.Idle"));
}
void Player::select(int clip) {
    if (clip<0 || clip>=int(catalog_.clips().size())) throw std::runtime_error("Clip not present in this variant");
    staticPiece_=-1; sequence_={clip}; seek(0);
}
void Player::selectPiece(int piece) {
    if(piece<0||piece>=int(catalog_.pieces.size()))throw std::runtime_error("Invalid piece");
    staticPiece_=piece;sequence_.clear();seek(0);
}
void Player::transform(bool toVehicle, bool running) {
    staticPiece_=-1;
    if(catalog_.kind("vehicle")<0) {
        select(catalog_.clip(toVehicle ? (running ? "FlySet.Trans_RobotRun2Vehicle" : "FlySet.Trans_Robot2Vehicle") : "FlySet.Trans_Vehicle2Robot"));
        return;
    }
    const std::string prefix=toVehicle ? "DriveSet.Trans_R2V_" : "Vehicle2RobotSet.Trans_V2R_";
    const std::string run=running ? "Run_" : "";
    sequence_={catalog_.clip(prefix+run+"A"),catalog_.clip(prefix+run+"B")};
    for (int c : sequence_) if(c<0) throw std::runtime_error("Transformation clip absent from this variant");
    seek(0);
}
float Player::duration() const {
    float seconds=0;
    for(int c:sequence_) seconds+=catalog_.clips()[c].at("length_ticks").get<float>()/(60*catalog_.clips()[c].value("speed",1.f));
    return seconds;
}
void Player::event(const Json& e, float at) {
    fired.push_back(e);
    const std::string type=e.at("type");
    const int robot=catalog_.kind("robot"), vehicle=catalog_.kind("vehicle");
    if(type=="model") {
        tracks[robot].visible=e.at("show_robot");
        if(vehicle>=0) {
            tracks[vehicle].visible=e.at("show_vehicle");
            const auto animation=stringOr(e,"vehicle_animation");
            if(!animation.empty()) {
                tracks[vehicle].advance(at);
                tracks[vehicle].play(animation,at,true);
                tracks[vehicle].then=stringOr(e,"vehicle_then");
            }
        }
    } else if(type=="weapon_attach") {
        std::string file=stringOr(e,"weapon_file");
        // Null means current gun. The browser's default loadout is the right laser rifle.
        if(file.empty()) {
            const int gun=catalog_.kind("weapon");
            if(gun>=0) file=catalog_.pieces[gun].file;
        }
        const int index=catalog_.piece(file);
        if(index<0) return;
        auto& t=tracks[index];
        if(e.value("hide",false)) t.visible=false;
        if(e.value("unhide",false)) t.visible=true;
        t.attachment=stringOr(e,"node");
        auto animation=stringOr(e,"weapon_animation");
        if(!animation.empty()) {
            if(t.piece->animation(animation)<0 && t.piece->animation(animation+"_WeaponA")>=0) animation+="_WeaponA";
            t.advance(at); t.play(animation,at,true);
        }
        t.then=stringOr(e,"weapon_then");
        t.hideWhenDone=e.value("hide_when_done",false);
    }
}
void Player::seek(float seconds) {
    time=std::max(0.f,seconds); fired.clear(); root=glm::mat4(1);
    for(size_t i=0;i<tracks.size();++i) tracks[i].reset(catalog_.pieces[i]);
    const int robot=catalog_.kind("robot"), vehicle=catalog_.kind("vehicle");
    if(robot>=0) tracks[robot].play("IdleStandSet.Idle",0,false);
    if(vehicle>=0) {
        // Idle is partial. Seed from the actual A/B chain, not the legacy monolithic
        // Trans_R2V clip: that older clip keys extra tyre translations that A/B leave alone.
        auto& t=tracks[vehicle]; t.play("VehicleModel.Trans_R2V_A",0,false);
        const float endA=t.duration();t.play("VehicleModel.Trans_R2V_B",endA,true);
        const float endB=endA+t.duration();t.play("VehicleModel.V_Idle",endB,true);t.started=0;
    }
    if(staticPiece_>=0){tracks[staticPiece_].reset(catalog_.pieces[staticPiece_]);tracks[staticPiece_].visible=true;}
    float begin=0;
    for(size_t s=0;s<sequence_.size();++s) {
        const auto& c=catalog_.clips()[sequence_[s]];
        const int p=catalog_.piece(c.at("file"));
        const float rate=c.value("speed",1.f);
        const float length=c.at("length_ticks").get<float>()/(60*rate);
        if(s==0) tracks[p].visible=true;
        tracks[p].advance(begin);
        tracks[p].play(stringOr(c,"animation"),begin,catalog_.pieces[p].kind=="vehicle");
        tracks[p].rate=rate;
        const float until=std::min(time,begin+length);
        std::vector<Json> events=c.at("events").get<std::vector<Json>>();
        std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a["tick"]<b["tick"];});
        for(const auto& e:events) {
            const float at=begin+e.at("tick").get<float>()/(60*rate);
            if(at<=until+1e-6f) event(e,at);
        }
        if(rootMotion && c.contains("root_travel") && catalog_.pieces[p].kind!="camera_track" && catalog_.pieces[p].kind!="unbound_motion_carriers") {
            const auto& travel=c["root_travel"];
            const float tick=std::clamp((until-begin)*60*rate,0.f,float(travel.size()-1));
            const size_t a=size_t(tick),b=std::min(a+1,travel.size()-1);
            auto vector=[&](size_t i){return glm::vec3(travel[i][0].get<float>(),travel[i][1].get<float>(),travel[i][2].get<float>());};
            const auto source=glm::mix(vector(a),vector(b),tick-float(a));
            // The export's root swaps Y/Z and negates X (proper RH rotation).
            const glm::vec3 position(-source.x,source.z,source.y);
            float angle=0;
            if(c.contains("root_turn_degrees")) {
                const auto& turn=c["root_turn_degrees"];
                const float first=turn[a], last=turn[b];
                angle=first+std::remainder(last-first,360.f)*(tick-float(a));
            }
            root=root*glm::translate(glm::mat4(1),position)*glm::rotate(glm::mat4(1),glm::radians(angle),glm::vec3(0,1,0));
        }
        begin+=length;
        if(time<begin || s+1==sequence_.size()) break;
    }
    poses.clear();
    for(auto& t:tracks) {
        t.advance(time);
        if(t.visible)poses.push_back(t.pose(time));else poses.emplace_back();
    }
}
}
