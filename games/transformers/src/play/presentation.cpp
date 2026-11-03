#include "presentation.hpp"
#include <imgui.h>
namespace transformers::play {
void View::update(const Controller& player,const Arena& arena) {
    const glm::vec3 target=player.position+glm::vec3(0,player.vehicle?3.5f:6.f,0);
    auto offset=glm::vec3(-std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch));
    float reach=distance;
    if(auto wall=arena.ray(target,offset,reach))reach=std::max(3.f,wall->distance-1);
    camera.position=target+offset*reach;auto direction=glm::normalize(target-camera.position);
    camera.yaw=std::atan2(direction.x,direction.z);camera.pitch=std::asin(direction.y);camera.farPlane=360;camera.nearPlane=.2f;
}
void hud(const Controller& p,const Arena& arena,bool help) {
    const auto& io=ImGui::GetIO();
    ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowBgAlpha(.85f);
    ImGui::Begin("Field test",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
    ImGui::TextColored({.2f,.9f,.9f,1},"OPTIMUS / PROVING GROUND");
    ImGui::Text("%s  |  %.0f km/h  |  %s",p.transforming?"TRANSFORMING":p.vehicle?"TRUCK":"ROBOT",std::abs(p.speed)*3.6f,p.vehicle?"TRUCK CANNONS":p.weapons.shell()?"SHELL PAIR":"DUAL LASERS");
    ImGui::Separator();ImGui::Text("Circuit: checkpoint %d / %zu    Laps: %d",p.nextCheckpoint+1,arena.checkpoints.size(),p.laps);
    ImGui::Text("Time %.1fs   Best %.1fs",p.lapTime,p.bestLap);
    ImGui::Text("Target crates: %d / %zu   Hits: %d",arena.completedTargets(),arena.targets.size(),p.shotsHit);
    ImGui::ProgressBar(p.weapons.heat(),{200,8},"");
    ImGui::TextDisabled("Hit each orange crate three times. Drive through teal pads.");
    ImGui::End();
    auto* draw=ImGui::GetForegroundDrawList();
    const ImVec2 origin(io.DisplaySize.x-230,20);const float scale=.85f;
    auto map=[&](glm::vec3 v){return ImVec2(origin.x+105+v.x*scale,origin.y+105-v.z*scale);};
    draw->AddRectFilled(origin,{origin.x+210,origin.y+210},IM_COL32(9,19,26,230),10);
    draw->AddRect(map({-65,0,65}),map({65,0,-65}),IM_COL32(80,100,110,255),3,0,14);
    for(size_t i=0;i<arena.checkpoints.size();++i)draw->AddCircleFilled(map(arena.checkpoints[i]),i==p.nextCheckpoint?7.f:4.f,i==p.nextCheckpoint?IM_COL32(50,245,215,255):IM_COL32(75,130,135,255));
    for(auto& t:arena.targets)if(auto* e=arena.world.find(t.id))draw->AddCircleFilled(map(e->transform.position),3,t.hits>=3?IM_COL32(60,230,100,255):IM_COL32(250,140,40,255));
    const auto here=map(p.position);draw->AddCircleFilled(here,4,IM_COL32(240,245,255,255));
    draw->AddLine(here,{here.x+std::sin(p.heading)*13,here.y-std::cos(p.heading)*13},IM_COL32(255,255,255,255),2);
    draw->AddText({origin.x+14,origin.y+215},IM_COL32(160,210,215,255),"NORTH / TEST YARD");
    const ImVec2 centre(io.DisplaySize.x*.5f,io.DisplaySize.y*.5f);
    draw->AddCircle(centre,6,IM_COL32(230,245,255,180),16,1.5f);
    if(help) {
        ImGui::SetNextWindowPos({20,io.DisplaySize.y-116});ImGui::SetNextWindowBgAlpha(.85f);
        ImGui::Begin("Controls",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        ImGui::Text("WASD  move / drive     Shift  sprint / boost     Space  jump");
        ImGui::Text("T  transform     Left mouse  fire     Q  weapon     F  melee     R  respawn");
        ImGui::Text("Hold right mouse  look     Wheel  zoom     F1  help     Esc  pause");
        ImGui::TextDisabled("Prototype controls and collision; original-game handling is not reproduced.");ImGui::End();
    }
}
void shotGeometry(const Controller& player,std::vector<one::Vertex>& vertices) {
    vertices.clear();
    for(auto& shot:player.shots) {
        auto direction=glm::normalize(shot.to-shot.from);auto side=glm::cross(direction,glm::vec3(0,1,0));
        if(glm::length(side)<.01f)side={1,0,0};side=glm::normalize(side)*.12f;
        for(auto p:{shot.from-side,shot.from+side,shot.to+side,shot.from-side,shot.to+side,shot.to-side}) {
            one::Vertex v;v.position=p;v.color=shot.hit?glm::vec4(.3f,1,.5f,1):glm::vec4(1,.55f,.08f,1);vertices.push_back(v);
        }
    }
}
}
