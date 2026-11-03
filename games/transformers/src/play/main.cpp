#include "presentation.hpp"
#include "../audio.hpp"
#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <iostream>
using namespace transformers;
using namespace transformers::play;
int main(int argc,char** argv) {
    try {
        bool capture=false,validate=false,audition=false;std::string scripted;
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--capture")capture=true;else if(a=="--validate")validate=true;else if(a=="--audition")audition=true;else if(a=="--script"&&i+1<argc)scripted=argv[++i];else throw std::runtime_error("Unknown option: "+a);}
        Catalog catalog(TRANSFORMERS_DEFAULT_EXPORT);Arena arena;Controller controller(catalog,arena);View view;
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
        struct Quit{~Quit(){SDL_Quit();}}quit;
        auto* window=SDL_CreateWindow("The One | Optimus Proving Ground",1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());
        struct Window{SDL_Window* w;~Window(){SDL_DestroyWindow(w);}}windowGuard{window};
        ImGui::CreateContext();ImGui::StyleColorsDark();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().FontGlobalScale=1.15f;
        ImGui_ImplSDL3_InitForVulkan(window);struct UI{~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}}ui;
        one::Renderer renderer;renderer.initialize(window,validate);renderer.setTextureStreaming(false);renderer.setShadowDistance(150);
        Audio audio;one::RenderScene scene,characterScene;std::vector<one::Vertex> effects;one::Material tracer;tracer.unlit=true;
        bool running=true,paused=false,help=true;Input pending;float accumulator=0;int frames=0;auto last=SDL_GetTicksNS();
        while(running) {
            renderer.waitForFrame();SDL_Event e;
            while(SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if(e.type==SDL_EVENT_QUIT)running=false;
                if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)renderer.resize();
                if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){paused=true;SDL_SetWindowRelativeMouseMode(window,false);}
                if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_RIGHT&&!paused&&!ImGui::GetIO().WantCaptureMouse)SDL_SetWindowRelativeMouseMode(window,true);
                if(e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.button==SDL_BUTTON_RIGHT)SDL_SetWindowRelativeMouseMode(window,false);
                if(e.type==SDL_EVENT_MOUSE_MOTION&&SDL_GetWindowRelativeMouseMode(window)){view.yaw-=e.motion.xrel*.004f;view.pitch=std::clamp(view.pitch+e.motion.yrel*.003f,-.15f,1.15f);}
                if(e.type==SDL_EVENT_MOUSE_WHEEL&&!ImGui::GetIO().WantCaptureMouse)view.distance=std::clamp(view.distance-e.wheel.y*2,12.f,65.f);
                if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat) {
                    if(e.key.key==SDLK_ESCAPE){paused=!paused;SDL_SetWindowRelativeMouseMode(window,false);}
                    if(e.key.key==SDLK_F1)help=!help;
                    if(!paused){pending.transform|=e.key.key==SDLK_T;pending.jump|=e.key.key==SDLK_SPACE;pending.attack|=e.key.key==SDLK_F;pending.reset|=e.key.key==SDLK_R;pending.switchWeapon|=e.key.key==SDLK_Q;}
                }
            }
            auto now=SDL_GetTicksNS();float dt=std::min(.1f,float(now-last)/1e9f);last=now;
            const bool* keys=SDL_GetKeyboardState(nullptr);pending.forward=float(keys[SDL_SCANCODE_W])-float(keys[SDL_SCANCODE_S]);pending.right=float(keys[SDL_SCANCODE_D])-float(keys[SDL_SCANCODE_A]);
            pending.boost=keys[SDL_SCANCODE_LSHIFT];pending.viewYaw=view.yaw;pending.viewPitch=-view.pitch;
            view.update(controller,arena);pending.cameraAim=true;pending.aimOrigin=view.camera.position;pending.aimDirection=view.camera.forward();
            pending.fire=(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK)&&!ImGui::GetIO().WantCaptureMouse;
            if(!scripted.empty()) {
                paused=false;dt=1.f/60;pending={};pending.viewYaw=0;
                if(scripted=="drive"){pending.transform=frames==30;pending.forward=frames>110?1.f:0.f;pending.right=frames>140?.4f:0.f;}
                else if(scripted=="walk"){pending.forward=frames<145?1.f:0.f;pending.jump=frames==85;pending.boost=true;}
                else if(scripted=="melee")pending.attack=frames==60;
                else if(scripted=="weapons"){pending.fire=frames>20&&frames<165;pending.switchWeapon=frames==100;}
                else if(scripted=="weapons-side"){view.yaw=1.2f;pending.fire=frames>20&&frames<165;pending.switchWeapon=frames==100;}
                else if(scripted=="truck-weapons"){view.yaw=1.2f;pending.transform=frames==30;pending.fire=frames>110;}
            }
            if(!paused) {
                accumulator+=dt;
                while(accumulator>=1.f/60){controller.update(1.f/60,pending);for(const auto& sound:controller.soundEvents)if(!capture||audition)audio.trigger(catalog,sound);accumulator-=1.f/60;pending.jump=pending.transform=pending.attack=pending.reset=pending.switchWeapon=false;}
            }else{accumulator=0;pending.jump=pending.transform=pending.attack=pending.reset=false;}
            view.update(controller,arena);
            if(!renderer.beginFrame()){SDL_Delay(10);continue;}
            ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();hud(controller,arena,help);
            if(paused) {
                ImGui::SetNextWindowPos({600,340},ImGuiCond_Always);ImGui::SetNextWindowSize({400,220});ImGui::Begin("Paused",nullptr,ImGuiWindowFlags_NoResize);
                ImGui::Text("OPTIMUS PROVING GROUND");
                if(ImGui::Button("Resume [Esc]",{-1,36}))paused=false;
                if(ImGui::Button("Respawn",{-1,36})){controller.reset();paused=false;}
                if(ImGui::Button("Quit",{-1,36}))running=false;ImGui::End();
            }
            ImGui::Render();audio.sync(controller.character,!paused&&(!capture||audition));
            if(!paused&&(!capture||audition)){if(controller.vehicle)audio.trigger(catalog,"VEH_OPTIMUS_MACK_RPM");else audio.stop("VEH_OPTIMUS_MACK_RPM");}
            arena.world.prepare(controller.clock,scene);controller.character.prepare(characterScene);appendScene(scene,characterScene);
            shotGeometry(controller,effects);std::vector<one::Draw> effectDraws;
            if(!effects.empty()){one::Draw draw;draw.count=uint32_t(effects.size());draw.material=&tracer;effectDraws.push_back(draw);}
            const int captureFrame=scripted.empty()?90:180;
            if(renderer.render(scene,effects,effectDraws,view.camera,controller.clock,capture&&frames==captureFrame,true,0,true))++frames;
            if(capture&&frames>captureFrame+2)running=false;
        }
        std::cout<<"PLAY RESULT position="<<controller.position.x<<','<<controller.position.y<<','<<controller.position.z<<" vehicle="<<controller.vehicle<<" validation_errors="<<renderer.errors()<<" audio_voices_started="<<audio.startedVoices()<<'\n';
        const auto errors=renderer.errors();renderer.close();return errors?1:0;
    }catch(const std::exception& e){std::cerr<<"Proving ground: "<<e.what()<<'\n';return 1;}
}
