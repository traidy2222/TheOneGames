#include "simulation.hpp"
#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>

namespace {
using glm::vec3;using glm::vec4;using one::Vertex;
one::Image turbulenceTexture(){
    one::Image image;image.name="Linear 3D turbulence lattice";image.width=image.height=256;image.rgba.resize(256*256*4);
    auto hash=[](unsigned x,unsigned y){unsigned v=(x&255u)*374761393u+(y&255u)*668265263u+72849u;v=(v^(v>>13))*1274126177u;return uint8_t((v^(v>>16))>>24);};
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto i=(y*256+x)*4;image.rgba[i]=hash(x,y);image.rgba[i+1]=hash(x+37,y+17);image.rgba[i+2]=0;image.rgba[i+3]=255;}
    return image;
}
void triangle(std::vector<Vertex>& out,vec3 a,vec3 b,vec3 c,vec3 color){
    auto n=glm::normalize(glm::cross(b-a,c-a));for(auto p:{a,b,c})out.push_back({p,n,vec4(color,1),{0,0}});
}
void quad(std::vector<Vertex>& out,vec3 a,vec3 b,vec3 c,vec3 d,vec3 color){triangle(out,a,b,c,color);triangle(out,a,c,d,color);}
void box(std::vector<Vertex>& out,vec3 p,vec3 s,vec3 color){
    vec3 a=p+vec3(-s.x,0,-s.z),b=p+vec3(s.x,0,-s.z),c=p+vec3(s.x,0,s.z),d=p+vec3(-s.x,0,s.z),u{0,s.y,0};
    quad(out,a,b,b+u,a+u,color);quad(out,b,c,c+u,b+u,color*.9f);quad(out,c,d,d+u,c+u,color);quad(out,d,a,a+u,d+u,color*.9f);quad(out,a+u,b+u,c+u,d+u,color);
}
void line(std::vector<Vertex>& out,vec3 a,vec3 b,float width,vec3 color){
    auto d=glm::normalize(b-a);auto side=glm::cross(d,vec3(0,1,0));if(glm::length(side)<.1f)side={1,0,0};else side=glm::normalize(side);side*=width;
    quad(out,a-side,b-side,b+side,a+side,color);
}
std::vector<Vertex> landscape(){
    std::vector<Vertex> out;
    for(int z=-2400;z<2200;z+=24)for(float x:{-454.f,-426.f}){
        box(out,{x,0,float(z)},{.11f,1.5f,.11f},{.21f,.19f,.14f});
        for(float y:{.55f,1.1f})line(out,{x,y,float(z)},{x,y,float(z+24)},.015f,{.19f,.20f,.18f});
    }
    for(int z=-2100;z<2600;z+=95){
        box(out,{-462,0,float(z)},{.16f,10,.16f},{.20f,.16f,.12f});
        box(out,{-462,8.7f,float(z)},{2,.15f,.16f},{.22f,.20f,.16f});
        for(float x:{-463.5f,-460.5f})for(int j=0;j<10;++j){float a=j/10.f,b=(j+1)/10.f;
            line(out,{x,9.f-2.f*std::sin(a*3.14159f),z+a*95},{x,9.f-2.f*std::sin(b*3.14159f),z+b*95},.025f,{.10f,.12f,.11f});
        }
    }
    for(int k=0;k<4;++k){vec3 p{-780.f-k*55,0,240.f+k*20};
        vec3 wall=k==0?vec3(.46f,.40f,.30f):vec3(.32f,.19f,.13f);
        box(out,p,{12,6,8},wall);
        quad(out,p+vec3(-14,6,-10),p+vec3(14,6,-10),p+vec3(14,11,0),p+vec3(-14,11,0),{.16f,.19f,.19f});
        quad(out,p+vec3(-14,11,0),p+vec3(14,11,0),p+vec3(14,6,10),p+vec3(-14,6,10),{.13f,.16f,.16f});
        triangle(out,p+vec3(-12,6,-8),p+vec3(-12,6,8),p+vec3(-12,11,0),wall);
        for(float x:{-7.f,6.f})box(out,p+vec3(x,2,-8.1f),{1.2f,1.6f,.08f},{.08f,.12f,.13f});
    }
    uint32_t seed=42;auto rand=[&](){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
    for(int i=0;i<130;++i){vec3 p{-1350+rand()*2600,0,400+rand()*2300};if(glm::length(glm::vec2(p.x,p.z))<650)continue;
        float h=8+rand()*10;box(out,p,{.35f,h*.65f,.35f},{.12f,.12f,.08f});
        for(int k=0;k<3;++k){vec3 c=p+vec3((rand()-.5f)*6,h*.7f+k*2,(rand()-.5f)*6);float r=3.5f+rand()*2;
            for(int j=0;j<8;++j){float a=j*6.2831853f/8,b=(j+1)*6.2831853f/8;vec3 v{std::cos(a)*r,0,std::sin(a)*r},w{std::cos(b)*r,0,std::sin(b)*r};
                triangle(out,c+v,c+w,c+vec3(0,r*1.4f,0),{.095f,.14f,.075f});triangle(out,c+w,c+v,c-vec3(0,r*.7f,0),{.08f,.11f,.065f});
            }
        }
    }
    return out;
}
void theme(){ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=8;s.FrameRounding=4;s.WindowPadding={18,15};s.ItemSpacing={9,10};
    s.Colors[ImGuiCol_WindowBg]={.035f,.049f,.048f,.92f};s.Colors[ImGuiCol_Border]={.3f,.38f,.32f,.25f};
    s.Colors[ImGuiCol_FrameBg]={.12f,.17f,.15f,1};s.Colors[ImGuiCol_SliderGrab]={.65f,.75f,.48f,1};s.Colors[ImGuiCol_Button]={.18f,.25f,.20f,1};
}
void view(one::Camera& c,int preset,float time=18){
    vec3 target{0,550,0};
    if(preset==0)c.position={-455,7,-2200};
    if(preset==1){c.position={-600,40,-950};target={0,420,0};}
    if(preset==2){c.position={1800,1050,-2400};target={0,650,0};}
    if(preset==3){c.position={600,130,-650};target={0,350,0};}
    auto axis=tornado::center(0,time);vec3 offset{axis.x,0,axis.y};target+=offset;c.position+=offset;
    auto d=glm::normalize(target-c.position);c.yaw=std::atan2(d.x,d.z);c.pitch=std::asin(d.y);
}
}
int main(int argc,char** argv){try{
    bool smoke=false,validate=false,ui=true,paused=false,orbit=false,cycle=false,benchmark=false,sweep=false;
    int preset=0,quality=128,benchmarkFrames=360;float fixedTime=18,initialMaturity=1,initialHumidity=.72f,azimuth=-999,elevation=30;
    std::filesystem::path outputDir=".";
    for(int i=1;i<argc;++i){std::string a=argv[i];
        if(a=="--smoke-test")smoke=true;else if(a=="--sweep"){smoke=true;sweep=true;}
        else if(a=="--benchmark")benchmark=true;else if(a=="--validate")validate=true;else if(a=="--clean")ui=false;
        else if(a=="--view"&&i+1<argc)preset=std::stoi(argv[++i]);
        else if(a=="--time"&&i+1<argc)fixedTime=std::stof(argv[++i]);
        else if(a=="--azimuth"&&i+1<argc)azimuth=std::stof(argv[++i]);
        else if(a=="--height"&&i+1<argc)elevation=std::stof(argv[++i]);
        else if(a=="--quality"&&i+1<argc)quality=std::clamp(std::stoi(argv[++i]),48,192);
        else if(a=="--humidity"&&i+1<argc)initialHumidity=std::clamp(std::stof(argv[++i]),.45f,.96f);
        else if(a=="--frames"&&i+1<argc)benchmarkFrames=std::max(30,std::stoi(argv[++i]));
        else if(a=="--output-dir"&&i+1<argc)outputDir=argv[++i];
        else if(a=="--maturity"&&i+1<argc)initialMaturity=std::clamp(std::stof(argv[++i]),0.f,1.f);
        else throw std::runtime_error("Unknown argument "+a);
    }
    std::filesystem::create_directories(outputDir);
    if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    struct Quit{~Quit(){SDL_Quit();}}quit;
    SDL_Window* window=SDL_CreateWindow("TheOne | TORNADO - Field Lab",1440,900,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
    if(!window)throw std::runtime_error(SDL_GetError());struct Window{SDL_Window* p;~Window(){SDL_DestroyWindow(p);}}windowGuard{window};
    ImGui::CreateContext();theme();ImGui::GetIO().IniFilename=nullptr;
    auto fontPath="C:/Windows/Fonts/segoeui.ttf";if(std::filesystem::exists(fontPath))ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath,18);
    if(!ImGui_ImplSDL3_InitForVulkan(window))throw std::runtime_error("SDL UI initialization failed");
    struct UI{~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}}uiGuard;
    one::Renderer renderer;renderer.initialize(window,validate);renderer.setCulling(false);renderer.setVsync(true);renderer.setTextureStreaming(false);
    one::Camera camera;camera.nearPlane=.5f;camera.farPlane=20000;view(camera,preset,fixedTime);
    tornado::Simulation sim;sim.parameters.maturity=initialMaturity;sim.parameters.humidity=initialHumidity;if(smoke||initialMaturity!=1)sim.reset(fixedTime);auto props=landscape();
    auto noise=turbulenceTexture();
    one::Material volume;volume.name="Tornado volume";volume.unlit=true;
    one::Material solid;solid.name="Landscape and windborne debris";volume.metallicRoughness=&noise;solid.metallicRoughness=&noise;
    bool running=true,controls=false,profiler=false,capture=false;int frames=0;float accumulator=0,fps=60,cycleTime=0;
    auto previous=SDL_GetTicksNS();double measured=0,gpuMeasured=0;unsigned measuredFrames=0,gpuSamples=0;
    while(running){
        if(!renderer.waitForFrame()){SDL_Delay(10);continue;}
        SDL_Event e;while(SDL_PollEvent(&e)){
            ImGui_ImplSDL3_ProcessEvent(&e);
            if(e.type==SDL_EVENT_QUIT)running=false;
            if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)renderer.resize();
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_RIGHT&&!ImGui::GetIO().WantCaptureMouse){SDL_SetWindowRelativeMouseMode(window,true);orbit=false;}
            if((e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.button==SDL_BUTTON_RIGHT)||e.type==SDL_EVENT_WINDOW_FOCUS_LOST)SDL_SetWindowRelativeMouseMode(window,false);
            if(e.type==SDL_EVENT_MOUSE_MOTION&&SDL_GetWindowRelativeMouseMode(window)){camera.yaw-=e.motion.xrel*.002f;camera.pitch=std::clamp(camera.pitch-e.motion.yrel*.002f,-1.5f,1.5f);}
            if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat&&!ImGui::GetIO().WantCaptureKeyboard){
                auto key=e.key.key;if(key==SDLK_ESCAPE)running=false;if(key==SDLK_TAB)controls=!controls;if(key==SDLK_H)ui=!ui;
                if(key==SDLK_SPACE)paused=!paused;if(key==SDLK_O)orbit=!orbit;if(key==SDLK_F2)profiler=!profiler;if(key==SDLK_F12)capture=true;
                if(key>=SDLK_1&&key<=SDLK_4){preset=int(key-SDLK_1);view(camera,preset,sim.time);orbit=false;}
            }
        }
        auto now=SDL_GetTicksNS();float elapsed=float(now-previous)*1e-9f;previous=now;float dt=std::min(elapsed,.1f);
        fps=fps*.94f+.06f/std::max(.001f,elapsed);
        if(frames>8){measured+=elapsed;++measuredFrames;auto stats=renderer.frameStats();if(stats.gpuTimingAvailable){gpuMeasured+=stats.gpuTotalMs;++gpuSamples;}}
        if(!smoke&&!ImGui::GetIO().WantCaptureKeyboard){auto keys=SDL_GetKeyboardState(nullptr);auto forward=camera.forward(),right=glm::normalize(glm::cross(forward,vec3(0,1,0)));
            auto move=forward*float(keys[SDL_SCANCODE_W]-keys[SDL_SCANCODE_S])+right*float(keys[SDL_SCANCODE_D]-keys[SDL_SCANCODE_A])+vec3(0,1,0)*float(keys[SDL_SCANCODE_E]-keys[SDL_SCANCODE_Q]);
            camera.position+=move*dt*(keys[SDL_SCANCODE_LSHIFT]?450.f:95.f);camera.position.y=std::clamp(camera.position.y,2.f,4000.f);
        }
        if(smoke){sim.time=fixedTime;}
        else if(!paused){accumulator+=dt;while(accumulator>=1.f/120.f){sim.step(1.f/120.f);accumulator-=1.f/120.f;}
            if(cycle){cycleTime+=dt;sim.parameters.maturity=std::clamp(std::min(cycleTime/35.f,(180.f-cycleTime)/40.f),0.f,1.f);if(cycleTime>190){cycleTime=0;sim.reset();}}
        }
        if((orbit&&!paused)||sweep||azimuth>-998){
            float a=sweep?float(frames/16)*6.2831853f/12:azimuth>-998?azimuth*.0174532925f:sim.time*.022f;
            auto axis=tornado::center(0,sim.time);camera.position={axis.x+std::sin(a)*1800,sweep||azimuth>-998?elevation:180,axis.y+std::cos(a)*1800};
            auto d=glm::normalize(vec3(axis.x,550,axis.y)-camera.position);camera.yaw=std::atan2(d.x,d.z);camera.pitch=std::asin(d.y);
        }
        if(!renderer.beginFrame())continue;ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
        auto& io=ImGui::GetIO();auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings;
        if(ui){
            ImGui::SetNextWindowPos({24,24});ImGui::SetNextWindowBgAlpha(.73f);ImGui::Begin("Title",nullptr,flags);
            ImGui::TextColored({.75f,.84f,.63f,1},"THE ONE   /   ATMOSPHERIC FIELD LAB");
            ImGui::SetWindowFontScale(1.8f);ImGui::TextUnformatted("TORNADO");ImGui::SetWindowFontScale(1);
            ImGui::TextDisabled("01   /   GREAT PLAINS     -     VOLUMETRIC STUDY");
            ImGui::End();
            ImGui::SetNextWindowPos({24,io.DisplaySize.y-104});ImGui::SetNextWindowBgAlpha(.78f);ImGui::Begin("Status",nullptr,flags);
            ImGui::TextColored({.75f,.84f,.63f,1},paused?"PAUSED":"LIVE");ImGui::SameLine();ImGui::Text("  %.0f m/s peak   |   %.0f m core radius   |   %.0f fps",sim.parameters.peakWind*sim.parameters.maturity,sim.parameters.radius,fps);
            ImGui::TextDisabled("RMB look   WASD fly   Q/E height   Shift boost   1-4 views   O orbit");
            ImGui::TextDisabled("Tab controls   Space pause   H hide UI   F12 capture   Esc exit");ImGui::End();
        }
        if(ui&&controls){
            ImGui::SetNextWindowPos({io.DisplaySize.x-350,24},ImGuiCond_Always);ImGui::SetNextWindowSize({326,0});ImGui::Begin("Field controls",nullptr,ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextWrapped("An adjustable vortex model in real-world metres. Not a predictive weather solver.");ImGui::Separator();
            ImGui::SliderFloat("Wind (m/s)",&sim.parameters.peakWind,15,115,"%.0f");
            ImGui::SliderFloat("Core (m)",&sim.parameters.radius,35,230,"%.0f");
            ImGui::SliderFloat("Humidity",&sim.parameters.humidity,.45f,.96f,"%.2f");
            if(ImGui::SliderFloat("Development",&sim.parameters.maturity,0,1,"%.2f"))cycle=false;
            ImGui::SliderInt("Volume detail",&quality,48,192);
            if(ImGui::Checkbox("Automatic lifecycle (190 s)",&cycle)){cycleTime=0;sim.parameters.maturity=cycle?0:1;}
            ImGui::Checkbox("Orbit camera",&orbit);ImGui::Checkbox("Pause",&paused);
            if(ImGui::Button("Reset debris"))sim.reset(sim.time);ImGui::SameLine();if(ImGui::Button("Roadside view"))view(camera,0,sim.time);
            auto local=tornado::wind(camera.position,sim.time,sim.parameters);
            ImGui::Separator();ImGui::Text("Local wind: %.1f m/s",glm::length(local));ImGui::Text("Altitude: %.0f m AGL",camera.position.y);ImGui::Text("Tracked debris: %zu",sim.debris.size());
            ImGui::TextWrapped("Condensation estimates pressure cooling and humidity. Turbulence and the parent storm are prescribed. Buildings are scale references.");ImGui::End();
        }
        if(profiler)renderer.drawProfiler(&profiler);ImGui::Render();
        volume.color={sim.parameters.peakWind,sim.parameters.radius,sim.parameters.maturity,float(quality)};solid.color=volume.color;
        volume.roughness=solid.roughness=sim.parameters.humidity;
        std::vector<Vertex> vertices;vertices.reserve(props.size()+sim.debris.size()*3+3);
        for(auto p:{vec3(-1,-1,0),vec3(3,-1,0),vec3(-1,3,0)})vertices.push_back({p,{0,1,0},{1,1,1,2},{0,0}});
        vertices.insert(vertices.end(),props.begin(),props.end());
        for(const auto& d:sim.debris){float a=d.phase;vec3 u{std::cos(a)*d.size,std::sin(a)*d.size*.4f,0},v{0,d.size*.3f,std::sin(a+1)*d.size*.6f};
            const vec3 colors[]={{.16f,.18f,.09f},{.5f,.47f,.38f},{.25f,.15f,.07f},{.35f,.37f,.36f}};
            triangle(vertices,d.position-u,d.position+u,d.position+v,colors[d.material]);
        }
        std::vector<one::Draw> draws{{0,3,&volume,nullptr,1},{3,uint32_t(vertices.size()-3),&solid,nullptr,2}};
        bool save=capture||(smoke&&(sweep?frames%16==11:frames==11));
        if(renderer.render(vertices,draws,camera,sim.time,save,false,0,true))++frames;
        if(save){capture=false;auto name=sweep?"angle-"+std::to_string((frames-1)/16)+".ppm":"view-"+std::to_string(preset)+".ppm";
            std::filesystem::copy_file("capture.ppm",outputDir/name,std::filesystem::copy_options::overwrite_existing);
            std::cout<<"Captured "<<name<<" at "<<sim.time<<" seconds\n";
        }
        if((smoke&&frames>=(sweep?192:14))||(benchmark&&frames>=benchmarkFrames))running=false;
    }
    SDL_SetWindowRelativeMouseMode(window,false);renderer.close();
    if(renderer.errors())throw std::runtime_error("Vulkan validation reported errors");
    std::cout<<"Rendered "<<frames<<" frames; sampled mean "<<(measured>0?measuredFrames/measured:0)<<" fps; GPU mean "<<(gpuSamples?gpuMeasured/gpuSamples:0)<<" ms\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"Tornado: "<<e.what()<<'\n';return 1;}}
