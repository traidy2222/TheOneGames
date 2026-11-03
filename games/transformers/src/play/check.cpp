#include "controller.hpp"
#include <iostream>
#include <stdexcept>
#include <theone/render/camera.hpp>
#include "aim.hpp"
using namespace transformers;
using namespace transformers::play;
void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Pass optimus.json");Catalog catalog(argv[1]);Arena arena;Controller p(catalog,arena);
        {
            Player aimPlayer(catalog);Weapons mounted(aimPlayer);
            aimPlayer.select(catalog.clip("WeaponSet.Weapon_Idle"));aimPlayer.seek(1.f);
            const int robot=catalog.kind("robot"),hand=catalog.pieces[robot].node("ObjectAttachment_Hand_R");
            const auto handPoint=glm::vec3(aimPlayer.poses[robot][hand][3]);
            mounted.prepare(false,true,0);
            for(bool shell:{false,true}) {
                mounted.setShell(shell);mounted.prepare(false,true,0);mounted.fire(false);
                for(float dt:{0.f,.05f,.1f}) {
                    mounted.prepare(false,true,dt);
                    const auto delta=mounted.muzzles(false).front()-handPoint;
                    std::cout<<"MUZZLE "<<(shell?"shell":"laser")<<" dt="<<dt<<" delta="<<delta.x<<','<<delta.y<<','<<delta.z<<'\n';
                    need(delta.z>2&&std::abs(delta.y)<1,"Weapon barrel is not forward in native idle/fire poses");
                }
            }
            aimPlayer.seek(1.f);applyAim(aimPlayer,1.134464f);mounted.prepare(false,true,0);
            const auto up=mounted.muzzles(false).front();
            aimPlayer.seek(1.f);applyAim(aimPlayer,-1.134464f);mounted.prepare(false,true,0);
            const auto down=mounted.muzzles(false).front();
            need(up.y>down.y+1,"Native additive aim does not raise/lower weapon muzzle");
        }
        Input input;
        auto steps=[&](int frames){for(int i=0;i<frames;++i){p.update(1.f/60,input);input.transform=input.jump=input.attack=input.reset=input.switchWeapon=false;}};
        steps(50);need(arena.physics.grounded(),"Spawn did not settle on floor");need(std::abs(p.position.y)<.25f,"Robot feet not on ground");
        const auto start=p.position;input.forward=1;steps(90);need(p.position.z>start.z+9,"Walking failed");
        input.forward=0;input.jump=true;steps(12);need(p.position.y>2,"Jump failed");steps(180);need(arena.physics.grounded(),"Jump never landed");
        input.transform=true;steps(90);need(p.vehicle&&!p.transforming,"Robot to truck failed");
        input.forward=1;input.right=.6f;steps(90);need(std::abs(p.heading)>.5f&&p.speed>15,"Driving/steering failed");
        input={};input.reset=true;steps(40);need(!p.vehicle&&glm::distance(p.position,arena.spawn)<.3f,"Respawn failed");
        input.right=1;const auto beforeRight=p.position;steps(20);need(p.position.x<beforeRight.x,"D moved screen-left with RH camera looking +Z");input={};p.reset();steps(20);
        Player sample(catalog);sample.select(catalog.clip("WalkSet.Walk"));sample.seek(.3f);PoseBlend blend;
        const int robot=catalog.kind("robot");const auto original=sample.poses[robot];blend.capture(sample);sample.select(catalog.clip("WalkSet.Run"));sample.seek(.2f);
        const auto targetPose=sample.poses[robot];blend.apply(sample,0);
        for(size_t n=0;n<original.size();++n)if(std::abs(glm::determinant(glm::mat3(original[n])))>.001f)
            need(glm::length(glm::vec3(original[n][3]-sample.poses[robot][n][3]))<.002f,"Blend start snapped");
        sample.poses[robot]=targetPose;blend.apply(sample,.1f);need(!blend.active(),"Blend did not finish");
        // Sweep toward the outer wall at speed: collision must prevent escape.
        arena.physics.resetPlayer((glm::vec3(100,centreHeight,0))/metresPerUnit);
        for(int i=0;i<100;++i)arena.step(1.f/60,{42,0,0},false);
        need(arena.position().x<106,"Outer collision wall tunneled");
        const auto* target=arena.world.find(arena.targets.front().id);need(target!=nullptr,"Missing target");
        const auto centre=target->transform.position;
        auto hit=arena.ray(centre-glm::vec3(12,0,0),{1,0,0},20);need(hit&&hit->id==target->id,"Target raycast failed");
        for(int i=0;i<3;++i)need(arena.hit(target->id,{1,.2f,0},5),"Target hit failed");
        need(arena.completedTargets()==1,"Target progress failed");
        arena.step(1.f/60,{},false);need(glm::distance(arena.world.find(target->id)->transform.position,centre)>.001f,"Target impulse failed");
        p.reset();steps(30);input.transform=true;steps(90);need(p.vehicle,"Race setup transform failed");
        for(auto checkpoint:arena.checkpoints){arena.physics.resetPlayer((checkpoint+glm::vec3(0,centreHeight,0))/metresPerUnit);steps(1);}
        need(p.laps==1&&p.nextCheckpoint==0,"Checkpoint circuit failed");
        p.reset();input={};input.viewYaw=1.5707963f;
        arena.physics.resetPlayer(glm::vec3(17,centreHeight,5)/metresPerUnit);steps(1);
        input.fire=true;steps(1);input.fire=false;
        need(p.shotsHit==1&&!p.shots.empty()&&p.shots.back().hit,"Player firing did not hit target");
        const auto muzzle=p.weapons.muzzles(false);need(muzzle.size()==2,"Dual weapons not mounted");
        need(std::any_of(muzzle.begin(),muzzle.end(),[&](auto m){return glm::distance(m,p.shots.back().from)<.001f;}),"Shot not emitted at rendered muzzle");
        need(arena.targets[1].hits==1,"Player firing hit wrong target");
        arena.physics.resetPlayer(glm::vec3(24,centreHeight,5)/metresPerUnit);steps(1);
        input.attack=true;steps(25);need(arena.targets[1].hits>=2,"Melee did not hit nearby target");
        p.reset();input={};steps(20);input.switchWeapon=true;input.fire=true;steps(20);input.fire=false;steps(1);
        need(p.weapons.shell()&&p.shots.size()==2,"Paired shell fire failed");
        input={};input.transform=true;steps(90);input.fire=true;steps(1);
        need(p.vehicle&&p.weapons.muzzles(true).size()==2,"Vehicle weapon mount failed");
        need(p.shots.size()==2,"Twin vehicle barrel fire failed");
        one::RenderScene worldScene,characterScene;arena.world.prepare(0,worldScene);p.character.prepare(characterScene);appendScene(worldScene,characterScene);
        for(auto& d:worldScene.draws)need(d.palette<worldScene.transforms.size(),"Combined draw palette out of range");
        std::cout<<"PASS: spawn, walking, jumping, transform, driving/steering, respawn, wall collision, raycast/target impulse, checkpoint lap, player shooting/melee and GPU palette assembly\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
