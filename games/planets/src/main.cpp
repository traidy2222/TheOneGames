#include "planet.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include <imgui_impl_sdl3.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <future>
#include <iomanip>
#include <sstream>

using namespace planets;
namespace {
void theme() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=9;s.FrameRounding=5;s.WindowPadding={18,16};s.ItemSpacing={8,9};
    s.Colors[ImGuiCol_WindowBg]={.025f,.045f,.06f,.96f};s.Colors[ImGuiCol_Button]={.08f,.24f,.27f,1};
    s.Colors[ImGuiCol_ButtonHovered]={.13f,.40f,.40f,1};s.Colors[ImGuiCol_FrameBg]={.08f,.12f,.15f,1};
    s.Colors[ImGuiCol_SliderGrab]={.3f,.85f,.65f,1};s.Colors[ImGuiCol_CheckMark]={.3f,.85f,.65f,1};
}
struct Session {
    Planet world;
    glm::dvec3 position{0};
    Frame frame{glm::dvec3(0,1,0)};
    glm::dvec3 anchor{0};
    one::Camera camera;
    Mesh mesh;
    bool orbit=false,cutaway=true,dirty=true;
    float brush=1.5f,latitude=90,longitude=0,depth=0;
    uint64_t revision=0,meshRevision=0;
    unsigned discardedBuilds=0;
    struct Build { Mesh mesh; Frame frame; glm::dvec3 anchor; double milliseconds; uint64_t revision; bool orbit; };
    std::future<Build> pending;
    int material=1;
    double meshMilliseconds=0;
    std::string status="Survey drone ready. Aim at the ground and excavate.";
    glm::dvec3 direction() const {
        constexpr double rad=3.141592653589793/180;
        double lat=latitude*rad,lon=longitude*rad;
        return {cos(lat)*sin(lon),sin(lat),cos(lat)*cos(lon)};
    }
    void land(bool bore=false) {
        orbit=false;mesh=Mesh{};++meshRevision;++revision;camera.nearPlane=.2f;camera.farPlane=terrainFarPlane;
        auto d=direction();position=d*(world.config().radius+world.height(d)-depth+(bore?0:4));
        if(bore) {
            auto count=world.dig(position,9);
            status="Survey bore: removed "+std::to_string(count)+" cells into inventory.";
        }
        frame=Frame(position);anchor=position;camera.position={0,0,0};camera.yaw=3.14159f;camera.pitch=-.28f;dirty=true;
    }
    void inspect() { camera.nearPlane=.1f;camera.farPlane=300;orbit=true;mesh=Mesh{};++meshRevision;++revision;camera.position={100,65,100};auto d=glm::normalize(-camera.position);camera.yaw=atan2(d.x,d.z);camera.pitch=asin(d.y);dirty=true; }
    void restore() {
        Planet next;next.load("saves/kestrel.json");next.refine(.5);
        world=std::move(next);depth=0;land();status="Saved planet restored at 0.50 m detail.";
    }
    void rebuild(bool blocking=false) {
        if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto result=pending.get();
            if(result.revision==revision&&result.orbit==orbit) {
                mesh=std::move(result.mesh);++meshRevision;meshMilliseconds=result.milliseconds;
                if(!orbit){frame=result.frame;anchor=result.anchor;camera.position=frame.local(position);}
                dirty=!orbit&&glm::distance(position,anchor)>8;
            }else ++discardedBuilds;
        }
        if(!dirty||pending.valid())return;
        auto build=[snapshot=world,p=position,f=Frame(position),mode=orbit,cut=cutaway,rev=revision]() mutable {
            auto start=std::chrono::steady_clock::now();
            auto geometry=mode?globeMesh(snapshot,cut):localMesh(snapshot,p,f);
            return Build{std::move(geometry),f,p,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),rev,mode};
        };
        dirty=false;
        if(blocking) {
            auto result=build();mesh=std::move(result.mesh);++meshRevision;meshMilliseconds=result.milliseconds;
            if(!orbit){frame=result.frame;anchor=result.anchor;camera.position=frame.local(position);}
        }else pending=std::async(std::launch::async,std::move(build));
    }
    double toolReach() const { return std::max(2.0,14.0-double(brush)); }
    std::optional<Hit> hit() const { return orbit?std::nullopt:world.ray(position,frame.direction(camera.forward()),toolReach()); }
    void excavate(bool deposit) {
        auto target=hit();if(!target){status="Move closer to excavate with this brush.";return;}
        if(deposit&&glm::distance(world.center(target->previous),position)<brush+1){status="Move back before depositing near the drone.";return;}
        uint64_t count=deposit?world.deposit(world.center(target->previous),brush,Material(material)):world.dig(world.center(target->cell),brush);
        std::ostringstream message;message<<(deposit?"Deposited ":"Excavated ")<<std::fixed<<std::setprecision(2)<<count*pow(world.config().cellSize,3)<<" m3.";status=message.str();
        if(count){dirty=true;++revision;}
    }
};
void ui(Session& s) {
    auto& io=ImGui::GetIO();const ImVec4 accent{.4f,.90f,.70f,1};
    auto action=[&](auto&& fn){try{fn();}catch(const std::exception& e){s.status=e.what();}};
    ImGui::SetNextWindowPos({18,18});ImGui::SetNextWindowSize({324,io.DisplaySize.y-36});
    ImGui::Begin("Planet survey",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(accent,"THE ONE  /  PLANET LAB");ImGui::Spacing();ImGui::Text("KESTREL");ImGui::TextDisabled("PROCEDURAL EXCAVATION PROTOTYPE");ImGui::Separator();
    ImGui::Text("Radius          %.0f km",s.world.config().radius/1000);
    ImGui::Text("Circumference   %.0f km",2*3.141592653589793*s.world.config().radius/1000);
    ImGui::Text("Excavation cell %.2f m",s.world.config().cellSize);
    ImGui::Text("Terrain reach   ~%.0f km",terrainReach/1000);
    ImGui::Text("Edited cells    %zu",s.world.edits().size());
    ImGui::Spacing();
    if(ImGui::Button("Surface survey",{138,32})) {s.depth=0;s.land();}
    ImGui::SameLine();if(ImGui::Button("Planet interior",{138,32}))s.inspect();
    if(s.orbit) {if(ImGui::Checkbox("Cutaway",&s.cutaway)){s.dirty=true;++s.revision;}ImGui::TextWrapped("Planet-wide material model. This diagram does not display individual excavation cells.");}
    else {
        ImGui::TextColored(accent,"EXCAVATION");ImGui::SliderFloat("Radius",&s.brush,.5f,8,"%.2f m");
        if(ImGui::BeginCombo("Deposit",properties(Material(s.material)).name)) {
            for(int i=1;i<int(materialCount);++i)if(ImGui::Selectable(properties(Material(i)).name,s.material==i))s.material=i;ImGui::EndCombo();
        }
        auto target=s.hit();if(target) {ImGui::Text("Target: %s",properties(target->material).name);ImGui::Text("Distance: %.1f m",target->distance);}
        else ImGui::TextDisabled("Tool reach: %.1f m",s.toolReach());
        ImGui::TextWrapped("Left click: dig. X: deposit from inventory. Hold right mouse to look.");
    }
    ImGui::Separator();ImGui::TextColored(accent,"SURVEY DESTINATION");
    ImGui::SliderFloat("Latitude",&s.latitude,-90,90,"%.1f deg");ImGui::SliderFloat("Longitude",&s.longitude,-180,180,"%.1f deg");
    ImGui::SliderFloat("Depth",&s.depth,0,float(s.world.config().radius*.98),"%.0f m",ImGuiSliderFlags_Logarithmic);
    if(ImGui::Button("Bore inspection chamber",{-1,30}))s.land(true);
    ImGui::TextWrapped("Survey shortcut; material goes to inventory.");
    ImGui::Separator();
    if(ImGui::Button("Save [F5]",{138,28}))action([&]{s.world.save("saves/kestrel.json");s.status="Planet edits and inventory saved.";});
    ImGui::SameLine();if(ImGui::Button("Load [F9]",{138,28}))action([&]{s.restore();});
    if(ImGui::CollapsingHeader("Material inventory",ImGuiTreeNodeFlags_DefaultOpen)) {
        bool any=false;
        for(size_t i=1;i<materialCount;++i)if(s.world.inventory()[i]) {any=true;auto c=glm::mix(properties(Material(i)).color,glm::vec3(1),.35f);ImGui::TextColored({c.r,c.g,c.b,1},"%s",properties(Material(i)).name);ImGui::SameLine();ImGui::Text("%.2f m3",s.world.inventory()[i]*pow(s.world.config().cellSize,3));}
        if(!any)ImGui::TextDisabled("Excavated material appears here.");
    }
    ImGui::Separator();ImGui::TextWrapped("%s",s.status.c_str());
    ImGui::TextDisabled("%.0f fps | %zu triangles",io.Framerate,s.mesh.vertices.size()/3);
    if(s.pending.valid())ImGui::TextColored(accent,"Updating terrain...");
    ImGui::TextDisabled("Last terrain rebuild: %.1f ms",s.meshMilliseconds);
    ImGui::End();
    ImGui::SetNextWindowPos({360,18});ImGui::SetNextWindowBgAlpha(.8f);
    ImGui::Begin("Mode",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
    ImGui::TextColored(accent,s.orbit?"INTERIOR / GEOLOGICAL MODEL":"SURFACE / EXCAVATION DRONE");
    if(s.orbit)ImGui::Text("Crust > hot solid mantle > molten outer core > solid inner core");
    else ImGui::Text("WASD fly  |  Q/E descend/ascend  |  Shift boost  |  O interior");
    ImGui::TextDisabled("Flow, heat transfer and structural collapse are not yet simulated.");ImGui::End();
    if(!s.orbit) {
        auto draw=ImGui::GetForegroundDrawList();ImVec2 center{io.DisplaySize.x*.5f,io.DisplaySize.y*.5f};
        draw->AddLine({center.x-7,center.y},{center.x+7,center.y},IM_COL32(180,245,220,230));
        draw->AddLine({center.x,center.y-7},{center.x,center.y+7},IM_COL32(180,245,220,230));
    }
}
}
int main(int argc,char** argv) {
    try {
        bool smoke=false,validate=false,startOrbit=false,asyncSmoke=false,distanceSmoke=false;
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--distance-smoke-test"){smoke=true;distanceSmoke=true;}else if(a=="--smoke-test")smoke=true;else if(a=="--async-smoke-test"){smoke=true;asyncSmoke=true;}else if(a=="--validate")validate=true;else if(a=="--orbit")startOrbit=true;else throw std::runtime_error("Unknown argument: "+a);}
        if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
        struct Quit{~Quit(){SDL_Quit();}}quit;
        auto window=SDL_CreateWindow("The One | Kestrel Planet Lab",1440,900,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());
        struct Window{SDL_Window* p;~Window(){SDL_DestroyWindow(p);}}guard{window};
        ImGui::CreateContext();theme();ImGui::GetIO().IniFilename=nullptr;
        if(!ImGui_ImplSDL3_InitForVulkan(window))throw std::runtime_error("Cannot initialize UI");
        struct UI{~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}}uiGuard;
        Renderer renderer;renderer.initialize(window,validate);
        Session s;s.land();if(distanceSmoke){s.position+=s.frame.up*120.0;s.camera.pitch=-.12f;s.dirty=true;}if(startOrbit)s.inspect();one::Material terrain;terrain.name="Planet material";one::Material sky;sky.name="Sky";sky.unlit=true;
        uint64_t renderedMeshRevision=UINT64_MAX,terrainUploadBytes=0;unsigned reusedTerrainFrames=0;
        bool running=true,forcedStaleBuild=false;int frames=0,lastSmokeAction=-1;auto previous=SDL_GetTicksNS();
        while(running) {
            auto safe=[&](auto&& fn){try{fn();}catch(const std::exception& e){if(smoke)throw;s.status=e.what();}};
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if(event.type==SDL_EVENT_QUIT)running=false;
                if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)renderer.resize();
                if(!smoke&&event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_RIGHT&&!ImGui::GetIO().WantCaptureMouse)SDL_SetWindowRelativeMouseMode(window,true);
                if((event.type==SDL_EVENT_MOUSE_BUTTON_UP&&event.button.button==SDL_BUTTON_RIGHT)||event.type==SDL_EVENT_WINDOW_FOCUS_LOST)SDL_SetWindowRelativeMouseMode(window,false);
                if(event.type==SDL_EVENT_MOUSE_MOTION&&SDL_GetWindowRelativeMouseMode(window)){s.camera.yaw-=event.motion.xrel*.003f;s.camera.pitch=std::clamp(s.camera.pitch-event.motion.yrel*.003f,-1.5f,1.5f);}
                if(!smoke&&event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT&&!ImGui::GetIO().WantCaptureMouse&&!s.orbit)safe([&]{s.excavate(false);});
                if(!smoke&&event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&!ImGui::GetIO().WantCaptureKeyboard) {
                    if(event.key.key==SDLK_ESCAPE)running=false;
                    if(event.key.key==SDLK_X&&!s.orbit)safe([&]{s.excavate(true);});
                    if(event.key.key==SDLK_O){if(s.orbit){s.depth=0;s.land();}else s.inspect();}
                    if(event.key.key==SDLK_F5)safe([&]{s.world.save("saves/kestrel.json");s.status="Planet saved.";});
                    if(event.key.key==SDLK_F9)safe([&]{s.restore();});
                }
            }
            auto now=SDL_GetTicksNS();float dt=std::min(.05f,float(now-previous)/1e9f);previous=now;
            if(!smoke&&!s.orbit&&!ImGui::GetIO().WantCaptureKeyboard) {
                const bool* keys=SDL_GetKeyboardState(nullptr);
                auto forward=s.camera.forward(),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
                glm::vec3 movement=forward*float(keys[SDL_SCANCODE_W]-keys[SDL_SCANCODE_S])+right*float(keys[SDL_SCANCODE_D]-keys[SDL_SCANCODE_A])+glm::vec3(0,1,0)*float(keys[SDL_SCANCODE_E]-keys[SDL_SCANCODE_Q]);
                if(glm::length(movement)>.01f) {
                    auto change=s.frame.direction(glm::normalize(movement))*double(dt*(keys[SDL_SCANCODE_LSHIFT]?24:8));
                    // Survey drone point collision; movement substeps prevent crossing thin cells.
                    int steps=std::max(1,int(std::ceil(glm::length(change)/.25)));
                    for(int step=0;step<steps;++step){auto next=s.position+change/double(steps);if(s.world.sample(next)==Material::Air)s.position=next;}
                }
                s.camera.position=s.frame.local(s.position);if(glm::distance(s.position,s.anchor)>8)s.dirty=true;
            }
            if(smoke&&frames!=lastSmokeAction) {
                lastSmokeAction=frames;
                if(distanceSmoke) {
                    if(frames==5){s.position+=s.frame.right*512.0;s.dirty=true;}
                    if(frames==12){s.latitude=35;s.longitude=120;s.land();s.position+=s.frame.up*120.0;s.camera.pitch=-.12f;s.dirty=true;}
                }else {
                if(frames==4)s.camera.pitch=-.65f;
                if(frames==5) {auto target=s.hit();if(!target)throw std::runtime_error("Smoke tool missed terrain");auto p=target->position-s.direction()*3.0;if(s.world.dig(p,5)==0)throw std::runtime_error("Smoke dig removed no terrain");s.dirty=true;++s.revision;}
                if(frames==10){s.world.save("smoke-planet.json");auto count=s.world.edits().size();s.world.load("smoke-planet.json");if(s.world.edits().size()!=count)throw std::runtime_error("Smoke save lost edits");}
                if(frames==12)s.inspect();
                }
            }
            s.rebuild(smoke&&!asyncSmoke);
            if(asyncSmoke&&!forcedStaleBuild&&s.pending.valid()) {
                if(s.world.dig(s.position-s.direction()*6.0,1.5)==0)throw std::runtime_error("Concurrent edit removed no terrain");
                ++s.revision;s.dirty=true;forcedStaleBuild=true;
            }
            if(!renderer.beginFrame()){SDL_Delay(10);continue;}
            ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();if(smoke)ImGui::BeginDisabled();safe([&]{ui(s);});if(smoke)ImGui::EndDisabled();ImGui::Render();
            terrain.unlit=s.orbit;
            std::vector<one::Draw> draws{{0,s.mesh.terrainVertices,&terrain,nullptr,1}};
            if(s.mesh.vertices.size()>s.mesh.terrainVertices)draws.push_back({s.mesh.terrainVertices,uint32_t(s.mesh.vertices.size()-s.mesh.terrainVertices),&sky,nullptr,2});
            bool settled=!s.pending.valid()&&!s.dirty;
            bool capture=smoke&&settled&&(frames==3||frames==8||frames==17);
            if(renderer.renderCached(s.mesh.vertices,draws,s.meshRevision,s.camera,0,capture,false,0,true)) {
                auto uploaded=renderer.geometryStats().uploadedDynamicBytes;terrainUploadBytes+=uploaded;
                if(renderedMeshRevision==s.meshRevision) {
                    if(smoke&&uploaded)throw std::runtime_error("Unchanged terrain was reuploaded");
                    ++reusedTerrainFrames;
                }
                renderedMeshRevision=s.meshRevision;
                if(capture)std::filesystem::copy_file("capture.ppm",frames==3?"surface.ppm":frames==8?"excavated.ppm":"interior.ppm",std::filesystem::copy_options::overwrite_existing);
                if(!smoke||settled)++frames;
            }
            if(smoke&&frames>=20)running=false;
        }
        SDL_SetWindowRelativeMouseMode(window,false);renderer.close();
        if(renderer.errors())throw std::runtime_error("Vulkan validation reported errors");
        if(asyncSmoke&&s.discardedBuilds==0)throw std::runtime_error("Stale mesh result was not rejected");
        if(smoke) {if(!reusedTerrainFrames)throw std::runtime_error("Terrain cache was not exercised");std::cout<<"Terrain uploads: "<<terrainUploadBytes<<" bytes; reused frames: "<<reusedTerrainFrames<<'\n';}
        if(smoke)std::cout<<"PASS: planet surface, excavation, persistent edits and interior rendered through TheOne Vulkan\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Planet Lab: "<<e.what()<<'\n';return 1;}
}
