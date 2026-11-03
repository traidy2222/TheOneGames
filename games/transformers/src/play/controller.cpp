#include "controller.hpp"
#include "aim.hpp"
#include <algorithm>
namespace transformers::play {
namespace {
glm::vec3 forward(float yaw){return {std::sin(yaw),0,std::cos(yaw)};}
void nodeDelta(const Piece& piece,std::vector<glm::mat4>& pose,int node,const glm::mat4& delta) {
    if(node<0)return;
    for(size_t i=0;i<pose.size();++i) {
        int n=int(i);while(n>=0&&n!=node)n=piece.model->nodes[n].parent;
        if(n==node)pose[i]=delta*pose[i];
    }
}
}
Controller::Controller(const Catalog& catalog,Arena& arena):arena_(arena),character(catalog),weapons(character){reset();}
void Controller::reset(){arena_.resetPlayer();position=arena_.spawn;speed=0;heading=0;gunTime_=100;fireCooldown_=wheelAngle_=takeoff_=landing_=0;airborne_=false;vehicle=false;transforming=attacking=false;clip("IdleStandSet.Idle",true);blend_.reset();shots.clear();}
void Controller::clip(const char* name,bool restart) {
    int next=character.catalog().clip(name);if(next>=0&&(restart||character.selected()!=next)) {
        blend_.capture(character,character.catalog().clips()[next].value("crossfade",character.catalog().data["animation_rules"].value("default_crossfade",.1f)));
        character.select(next);
    }
}
void Controller::fire() {
    const auto projectiles=weapons.fire(vehicle,chargeSeconds_);
    for(const auto& projectile:projectiles) {
    const auto start=projectile.origin;
    const auto look=glm::vec3(std::sin(aim_.viewYaw)*std::cos(aim_.viewPitch),std::sin(aim_.viewPitch),std::cos(aim_.viewYaw)*std::cos(aim_.viewPitch));
    const auto eye=aim_.cameraAim?aim_.aimOrigin:position+glm::vec3(0,vehicle?3.5f:6.f,0);
    const auto sight=aim_.cameraAim?aim_.aimDirection:look;
    const auto sightHit=arena_.ray(eye,sight,projectile.range);
    auto direction=glm::normalize((sightHit?sightHit->point:eye+sight*projectile.range)-start);
    float alignment=character.catalog().data["mechanics"]["constants"].value("aim_assist_hard",.81f);
    // Mild aim assistance keeps the small test targets usable with an orbit camera.
    for(const auto& target:arena_.targets)if(const auto* e=arena_.world.find(target.id)) {
        auto delta=e->transform.position-start;float range=glm::length(delta);
        if(range>2&&range<85){auto aim=delta/range;float dot=glm::dot(direction,aim);if(dot>alignment){alignment=dot;direction=aim;}}
    }
    auto hit=arena_.ray(start,direction,projectile.range);bool scored=false;
    if(hit){scored=arena_.hit(hit->id,direction+glm::vec3(0,.25f,0),projectile.damage*.8f);if(scored)++shotsHit;}
    shots.push_back({start,hit?hit->point:start+direction*projectile.range,.12f,scored});
    if(!projectile.sound.empty()&&std::find(soundEvents.begin(),soundEvents.end(),projectile.sound)==soundEvents.end())soundEvents.push_back(projectile.sound);
    }
    if(!projectiles.empty()){gunTime_=0;fireCooldown_=weapons.cooldown(vehicle);}
}
void Controller::wheels(float dt) {
    if(!vehicle||transforming)return;
    const auto& catalog=character.catalog();const int part=catalog.kind("vehicle");if(part<0)return;
    auto& pose=character.poses[part];const auto& piece=catalog.pieces[part];
    const float radius=catalog.data["mechanics"]["car"].value("render_wheel_radius",.56f);
    wheelAngle_=std::remainder(wheelAngle_+speed*dt/radius,6.2831853f);
    for(const auto& wheel:catalog.data["vehicle"]["wheels"]) {
        const int n=piece.node(wheel["node"]);if(n<0)continue;
        auto m=pose[n];nodeDelta(piece,pose,n,m*glm::rotate(glm::mat4(1),-wheelAngle_,glm::vec3(1,0,0))*glm::inverse(m));
    }
}
void Controller::update(float dt,const Input& input) {
    requestedFire_=false;aim_=input;soundEvents.clear();
    if(input.switchWeapon)weapons.setShell(!weapons.shell());
    const bool releaseShell=!vehicle&&weapons.shell()&&heldFire_&&!input.fire;
    if(!vehicle&&weapons.shell()&&input.fire)chargeSeconds_+=dt;
    else if(!releaseShell)chargeSeconds_=0;
    heldFire_=input.fire;
    clock+=dt;lapTime+=dt;fireCooldown_=std::max(0.f,fireCooldown_-dt);gunTime_+=dt;
    for(auto& shot:shots)shot.life-=dt;std::erase_if(shots,[](const auto& shot){return shot.life<=0;});
    if(input.reset)reset();
    if(input.transform&&!transforming&&!attacking&&arena_.physics.grounded()) {
        transforming=true;actionTime_=0;character.transform(!vehicle);speed=0;
    }
    if(input.attack&&!vehicle&&!transforming&&!attacking) {attacking=true;attackHit_=false;actionTime_=0;clip("CombatSet.AttackFast_1",true);}
    glm::vec3 movement(0);bool jump=false;
    if(transforming||attacking) {
        actionTime_+=dt;character.seek(std::min(actionTime_,character.duration()));
        if(attacking&&!attackHit_&&actionTime_>=.32f) {
            attackHit_=true;
            for(const auto& target:arena_.targets)if(auto* e=arena_.world.find(target.id)) {
                auto delta=e->transform.position-position;delta.y=0;
                if(glm::length(delta)<13&&glm::dot(glm::normalize(delta+glm::vec3(.001f,0,0)),forward(heading))>.2f)
                    if(arena_.hit(target.id,forward(heading)+glm::vec3(0,.8f,0),85.f))soundEvents.push_back("H2H_PUNCH_LG_METAL");
            }
        }
        if(actionTime_>=character.duration()) {
            if(transforming){vehicle=!vehicle;transforming=false;}attacking=false;
            clip(vehicle?"VehicleModel.V_Idle":"IdleStandSet.Idle",true);
        }
    } else {
        if(vehicle) {
            const auto& car=character.catalog().data["mechanics"]["car"];
            const float maximum=input.boost?car.value("max_turbo_speed",42.f):car.value("max_speed",35.f);
            const float desired=input.forward*(input.forward<0?car.value("max_reverse_speed",15.f):maximum);
            const float acceleration=input.forward==0?car.value("braking_deceleration",15.f):car.value("throttle",18.f)*(input.boost?car.value("turbo_throttle_boost",2.f):1.f);
            speed+=std::clamp(desired-speed,-acceleration*dt,acceleration*dt);
            if(std::abs(speed)>.5f)heading-=input.right*1.2f*dt*std::clamp(std::abs(speed)/10.f,0.f,1.f)*(speed<0?-1.f:1.f);
            movement=forward(heading)*speed;clip("VehicleModel.V_Idle");
        } else {
            auto desired=forward(input.viewYaw)*input.forward+glm::vec3(-std::cos(input.viewYaw),0,std::sin(input.viewYaw))*input.right;
            const float magnitude=glm::length(desired);speed=magnitude>.01f?(input.boost?21.f:8.f):0;
            if(magnitude>.01f){desired/=magnitude;float target=std::atan2(desired.x,desired.z);heading+=std::remainder(target-heading,6.2831853f)*std::min(1.f,dt*14);movement=desired*speed;}
            const bool grounded=arena_.physics.grounded();
            if(grounded&&airborne_)landing_=.12f;
            airborne_=!grounded;takeoff_=std::max(0.f,takeoff_-dt);landing_=std::max(0.f,landing_-dt);
            if(takeoff_>0){}
            else if(!grounded)clip(input.fire?(arena_.physics.playerVelocity().y>0?"WeaponSet.Weapon_JumpRising":"WeaponSet.Weapon_Fall"):(arena_.physics.playerVelocity().y>0?"JumpSet.Jump_InAir":"JumpSet.Jump_Fall"));
            else if(landing_>0)clip(input.fire?"WeaponSet.Weapon_FallLand":"IdleStandSet.Jump_Land");
            else if(input.fire||releaseShell){
                heading=input.viewYaw;
                if(magnitude>.01f)clip(std::abs(input.right)>std::abs(input.forward)?(input.right>0?"WeaponSet.Weapon_RunRight":"WeaponSet.Weapon_RunLeft"):(input.forward>0?"WeaponSet.Weapon_RunFwd":"WeaponSet.Weapon_RunBack"));
                else clip("WeaponSet.Weapon_Idle");
            }
            else clip(speed>10?"WalkSet.Run":speed>0?"WalkSet.Walk":"IdleStandSet.Idle");
            jump=input.jump&&grounded;if(jump){clip(input.fire?"WeaponSet.Weapon_JumpTakeOff":"JumpSet.Jump_Takeoff",true);takeoff_=character.duration();}
        }
        const float duration=character.duration();character.seek(duration>0?std::fmod(character.time+dt,duration):0);
        if((!vehicle&&weapons.shell()?releaseShell:input.fire)&&fireCooldown_<=0)requestedFire_=true;
    }
    arena_.step(dt,movement,jump);position=arena_.position();
    if(position.y<-15||glm::length(glm::vec2(position.x,position.z))>170)reset();
    if(vehicle&&glm::distance(glm::vec2(position.x,position.z),glm::vec2(arena_.checkpoints[nextCheckpoint].x,arena_.checkpoints[nextCheckpoint].z))<10) {
        if(++nextCheckpoint==int(arena_.checkpoints.size())){nextCheckpoint=0;++laps;if(bestLap==0||lapTime<bestLap)bestLap=lapTime;lapTime=0;}
    }
    blend_.apply(character,dt);
    if(!vehicle&&!transforming&&!attacking&&(input.fire||releaseShell))applyAim(character,input.viewPitch);
    wheels(dt);
    character.root=glm::translate(glm::mat4(1),position)*glm::rotate(glm::mat4(1),heading,glm::vec3(0,1,0));
    weapons.prepare(vehicle,!transforming&&!attacking&&(input.fire||requestedFire_||gunTime_<.4f),dt);
    if(requestedFire_)fire();
}
}
