#include "player.hpp"
#include "stage.hpp"
#include "audio.hpp"
#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <iostream>
#include <memory>
#include <algorithm>

using namespace transformers;
int main(int argc,char** argv) {
    try {
        std::filesystem::path asset=TRANSFORMERS_DEFAULT_EXPORT;
        bool capture=false,validate=false; std::string initial="IdleStandSet.Idle",staticPiece; float captureTime=0;
        for(int i=1;i<argc;++i) {
            const std::string a=argv[i];
            if(a=="--asset"&&i+1<argc)asset=argv[++i];
            else if(a=="--capture")capture=true;
            else if(a=="--validate")validate=true;
            else if(a=="--clip"&&i+1<argc)initial=argv[++i];
            else if(a=="--piece"&&i+1<argc)staticPiece=argv[++i];
            else if(a=="--time"&&i+1<argc)captureTime=std::stof(argv[++i]);
            else throw std::runtime_error("Unknown argument: "+a);
        }
        auto catalog=std::make_unique<Catalog>(asset);
        auto player=std::make_unique<Player>(*catalog);
        if(!staticPiece.empty())player->selectPiece(catalog->piece(staticPiece)>=0?catalog->piece(staticPiece):catalog->kind(staticPiece));
        else if(initial=="transform")player->transform(true);
        else if(initial=="untransform")player->transform(false);
        else player->select(catalog->clip(initial));
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
        struct Quit{~Quit(){SDL_Quit();}}quit;
        auto* window=SDL_CreateWindow("The One | Optimus Prime",1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());
        struct Window{SDL_Window* p;~Window(){SDL_DestroyWindow(p);}}windowGuard{window};
        ImGui::CreateContext(); ImGui::StyleColorsDark(); ImGui::GetIO().IniFilename=nullptr;
        if(!ImGui_ImplSDL3_InitForVulkan(window))throw std::runtime_error("UI initialization failed");
        struct UI{~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}}uiGuard;
        one::Renderer renderer; renderer.initialize(window,validate);
        // A character inspection view needs complete texture atlases even on tiny submeshes.
        // This affects only this viewer; the engine's residency policy/budgets are unchanged.
        renderer.setTextureStreaming(false);
        one::Camera camera; camera.farPlane=500;
        one::RenderScene scene;
        Stage stage; Audio audio;
        float yaw=.5f,pitch=.18f,distance=16.f,targetHeight=4.2f,speed=1;
        auto frameCharacter=[&]{const float height=catalog->data.at("summary").at("robot").at("height");distance=height*2;targetHeight=height*.5f;};
        frameCharacter();
        bool classic=true,fullDetail=true;
        bool playing=!capture,loop=true,running=true,profiler=false;
        char filter[128]{}; std::string status;
        auto previous=SDL_GetTicksNS(); int frames=0;
        while(running) {
            renderer.waitForFrame();
            SDL_Event e;
            while(SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if(e.type==SDL_EVENT_QUIT)running=false;
                if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)renderer.resize();
                if(e.type==SDL_EVENT_MOUSE_WHEEL&&!ImGui::GetIO().WantCaptureMouse)distance=std::clamp(distance-e.wheel.y*1.5f,3.f,150.f);
                if(e.type==SDL_EVENT_MOUSE_MOTION&&(e.motion.state&SDL_BUTTON_RMASK)&&!ImGui::GetIO().WantCaptureMouse) {
                    yaw-=e.motion.xrel*.006f; pitch=std::clamp(pitch+e.motion.yrel*.004f,-.6f,1.4f);
                }
                if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat&&!ImGui::GetIO().WantCaptureKeyboard) {
                    if(e.key.key==SDLK_ESCAPE)running=false;
                    if(e.key.key==SDLK_SPACE)playing=!playing;
                    if(e.key.key==SDLK_T) {player->transform(player->tracks[catalog->kind("robot")].visible); playing=true;loop=false;}
                }
            }
            auto now=SDL_GetTicksNS(); const float dt=std::min(.1f,float(now-previous)/1e9f); previous=now;
            if(playing) {
                float next=player->time+dt*speed;
                if(next>player->duration()&&player->duration()>0) {
                    if(loop)next=std::fmod(next,player->duration());
                    else {next=player->duration();playing=false;}
                }
                player->seek(next);
            }
            if(capture)player->seek(captureTime);
            if(!renderer.beginFrame()){SDL_Delay(10);continue;}
            ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos({16,16},ImGuiCond_Once); ImGui::SetNextWindowSize({390,920},ImGuiCond_Once);
            ImGui::Begin("Optimus Prime");
            ImGui::TextColored({.35f,.7f,1,1},"THE ONE / CHARACTER LAB");
            const std::string pack=catalog->data["character"]["pack"];
            if(ImGui::BeginCombo("Variant",pack.c_str())) {
                for(const char* name:{"optimus","optimusflight","optimusgold"})if(ImGui::Selectable(name,pack==name)) {
                    try {
                        auto replacement=std::make_unique<Catalog>(catalog->folder.parent_path()/name/(std::string(name)+".json"));
                        auto replacementPlayer=std::make_unique<Player>(*replacement);
                        audio.clear(); player=std::move(replacementPlayer); catalog=std::move(replacement);
                        frameCharacter();
                        renderer.trimUnusedAssets(); status.clear();
                    }catch(const std::exception& x){status=x.what();}
                }
                ImGui::EndCombo();
            }
            ImGui::Text("%zu pieces | %zu clip entries",catalog->pieces.size(),catalog->clips().size());
            if(ImGui::BeginCombo("Inspect piece","Select model / source geometry")) {
                for(size_t i=0;i<catalog->pieces.size();++i)if(ImGui::Selectable(catalog->pieces[i].file.c_str())) {
                    player->selectPiece(int(i));playing=false;
                    status=catalog->pieces[i].kind=="unbound_source_meshes" ? "Loose source meshes. Their animation bindings are unknown." : "";
                }
                ImGui::EndCombo();
            }
            if(ImGui::Button("Robot")){player->select(catalog->clip("IdleStandSet.Idle"));loop=true;playing=true;}
            const bool hasVehicle=catalog->kind("vehicle")>=0;
            ImGui::SameLine(); if(ImGui::Button(hasVehicle?"Truck":"Flight")){player->select(catalog->clip(hasVehicle?"VehicleModel.V_Idle":"FlySet.Fly_Idle"));loop=true;playing=true;}
            if(ImGui::Button(hasVehicle?"Transform to truck [T]":"Enter flight")){player->transform(true);playing=true;loop=false;}
            if(ImGui::Button("Transform to robot [T]")){player->transform(false);playing=true;loop=false;}
            ImGui::Checkbox("Play [Space]",&playing);ImGui::SameLine();ImGui::Checkbox("Loop",&loop);
            ImGui::SliderFloat("Speed",&speed,.05f,2,"%.2fx");
            float seek=std::min(player->time,player->duration());
            if(ImGui::SliderFloat("Time",&seek,0,player->duration(),"%.3f s")){playing=false;player->seek(seek);}
            if(ImGui::Checkbox("Root motion",&player->rootMotion))player->seek(player->time);
            ImGui::SameLine(); ImGui::Checkbox("Special body",&player->special);
            ImGui::Checkbox("Clip sounds",&audio.enabled);ImGui::SameLine();ImGui::Checkbox("Legacy lighting",&classic);
            if(ImGui::Checkbox("Full atlas detail",&fullDetail))renderer.setTextureStreaming(!fullDetail);
            ImGui::SliderFloat("Camera height",&targetHeight,-2,15);
            ImGui::TextDisabled("Right drag: orbit | Scroll: zoom");
            ImGui::Separator(); ImGui::InputText("Filter clips",filter,sizeof(filter));
            if(ImGui::BeginChild("clips",{0,420},true)) {
                for(size_t i=0;i<catalog->clips().size();++i) {
                    const auto& c=catalog->clips()[i]; const std::string name=c["name"];
                    if(filter[0]&&name.find(filter)==std::string::npos)continue;
                    const std::string label=name+"##"+std::to_string(i);
                    if(ImGui::Selectable(label.c_str(),player->selected()==int(i))) {
                        player->select(int(i));playing=true;
                        const auto& kind=catalog->pieces[catalog->piece(c["file"])].kind;
                        status=(kind=="unbound_motion_carriers"||kind=="camera_track") ? "Source motion markers. No mesh hierarchy is inferred." : "";
                    }
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",c["file"].get<std::string>().c_str(),c["animation"].is_null()?"Event-only clip":"Animated model");
                }
            } ImGui::EndChild();
            if(ImGui::CollapsingHeader("Events at current time"))for(const auto& ev:player->fired)ImGui::TextWrapped("%.1f: %s",ev["tick"].get<float>(),ev["type"].get<std::string>().c_str());
            ImGui::Checkbox("Renderer profiler",&profiler);
            if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
            ImGui::End(); if(profiler)renderer.drawProfiler(&profiler);
            audio.sync(*player,playing&&!capture);
            ImGui::Render();
            glm::vec3 target=glm::vec3(player->root[3])+glm::vec3(0,targetHeight,0);
            for(size_t p=0;p<player->tracks.size();++p)
                if(player->tracks[p].visible&&catalog->pieces[p].kind=="camera_track"&&player->poses[p].size()>1)
                    target=glm::vec3(player->poses[p][1][3]);
            camera.position=target+distance*glm::vec3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch));
            auto direction=glm::normalize(target-camera.position); camera.yaw=std::atan2(direction.x,direction.z);camera.pitch=std::asin(direction.y);
            player->prepare(scene);
            stage.fitBelow(scene,!player->tracks[catalog->kind("robot")].visible);
            stage.showMotionCarriers(*player);
            if(renderer.render(scene,stage.vertices,stage.draws,camera,player->time,capture&&frames==60,true,0,classic))++frames;
            if(capture&&frames>=63)running=false;
        }
        const auto errors=renderer.errors();renderer.close();
        if(errors)throw std::runtime_error("Vulkan validation errors: "+std::to_string(errors));
        return 0;
    }catch(const std::exception& e){std::cerr<<"Optimus: "<<e.what()<<'\n';return 1;}
}
