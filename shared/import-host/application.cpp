#include "frame_boundary.hpp"
#include "application.hpp"
#include "visible_profile.hpp"
#include <theone/scene/editor_history.hpp>
#include <theone/scene/picking.hpp>
#include "background_loading.hpp"
#include "scene_handoff.hpp"
#include <unordered_set>
#include "renderer.hpp"
#include "game.hpp"
#include "effects.hpp"
#include "input.hpp"
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
#include <deque>
#include <limits>

namespace {
struct Session {
    BackgroundLoads loading;
    SceneHandoff handoff;
    bool resumeAfterCollision=false;
    one::World world;
    one::ScenePicker picker;
    std::unique_ptr<one::Physics> physics=std::make_unique<one::Physics>();
    std::unique_ptr<one::GameModule> game=one::createSandboxGame();
    one::Camera camera;
    one::ParticleSystem effects;
    one::DoubleJump doubleJump;
    std::string robotAnimation;
    float robotAnimationTime=0;
    one::EditorHistory edits;
    bool gesture=false,gestureResume=false,gestureCancelledCollision=false;
    size_t collisionRequests=0;
    std::vector<std::string> log;
    std::vector<one::Vertex> vertices; std::vector<one::Draw> draws; one::RenderScene renderScene;
    bool simulate=true,walk=false,shadows=true,showEditor=true,animation=true,dirty=false,showProfiler=false;
    uint64_t selected=2;
    float time=0,accumulator=0;
    glm::vec3 desiredMovement{0}; bool jump=false;
    glm::vec3 respawn{0,2,10};
    SDL_AudioStream* audio=nullptr;
    SDL_AudioStream* music=nullptr;
    std::vector<uint8_t> sound;
    std::vector<uint8_t> musicData;
    std::filesystem::path savePath="saves/scene.one";
    std::string status="Ready";
    ~Session() { if(audio) SDL_DestroyAudioStream(audio); if(music) SDL_DestroyAudioStream(music); }
    void message(std::string text) { status=text; log.push_back(text); std::cout<<text<<std::endl; }
    void soundEffect() { if(audio) { if(!SDL_PutAudioStreamData(audio,sound.data(),static_cast<int>(sound.size()))) message(SDL_GetError()); } }
    one::GameServices services() { return {world,*physics,[this] { soundEffect(); }}; }
    void commitEdit() {
        auto result=edits.commit(world);dirty|=result.changed;
        if(result.collision)rebuild();
    }
    void endGesture() {
        if(!gesture)return;
        gesture=false;simulate=gestureResume;
        auto result=edits.commit(world);dirty|=result.changed;
        if(result.collision || gestureCancelledCollision)rebuild();
        gestureCancelledCollision=false;
    }
    void checkpoint() {endGesture();if(edits.pending())commitEdit();edits.begin(world);}
    void beginGesture(const one::Entity& entity,const one::Transform& before) {
        endGesture();if(edits.pending())commitEdit();
        auto original=entity;original.transform=before;edits.beginEntity(original);
        auto state=loading.collision.status().state;
        gestureCancelledCollision=state==one::LoadState::Queued || state==one::LoadState::Loading || state==one::LoadState::Ready;
        gesture=true;gestureResume=gestureCancelledCollision?resumeAfterCollision:simulate;
        simulate=false;loading.collision.cancel();
    }
    void retireHistory() {
        auto old=std::make_shared<one::EditorHistory>(std::move(edits));edits=one::EditorHistory{};
        gesture=false;loading.scene.retire(std::move(old));
    }
    void rebuildImmediate() { physics->rebuild(world); auto s=services(); game->start(s); accumulator=0; }
    void requestCollision(one::World target) {
        auto state=loading.collision.status().state;
        if(state!=one::LoadState::Queued && state!=one::LoadState::Loading && state!=one::LoadState::Ready)resumeAfterCollision=simulate;
        simulate=false;
        ++collisionRequests;loading.collision.request(physics->captureEdit(target));
    }
    void rebuild() { requestCollision(world); }
    void applyCollision(std::shared_ptr<one::PhysicsEdit> next) {
        physics->applyEdit(*next,world);
        loading.collision.retire(std::move(next));
        accumulator=0;simulate=resumeAfterCollision;auto s=services();game->start(s);
        message("Collision ready");
    }
    void applyScene(std::shared_ptr<one::PhysicsScene> next,Renderer& renderer) {
        auto path=next->world.source;
        next->physics->setRespawnPosition(respawn);next->physics->resetPlayer(respawn);
        handoff.apply(*next,world,physics,renderer,[&]{retireHistory();},[&]{
            time=0;accumulator=0;dirty=false;
            selected=world.entities.empty()?0:world.entities.front().id;
            auto servicesNow=services();game->start(servicesNow);
        });
        loading.scene.retire(std::move(next));message("Loaded "+path.string());
    }
    void load(std::filesystem::path path,Renderer& renderer) {loading.cancel();applyScene(one::PhysicsScene::load(path),renderer);}
    void requestScene(std::filesystem::path path) {loading.cancel();loading.scene.request(std::filesystem::absolute(path));}
    void reloadSelectedAsset() {
        auto* entity=world.find(selected);if(!entity||!entity->model)return;
        loading.importing=false;loading.model.request(std::filesystem::absolute(entity->model->source));
    }
    void import(const std::filesystem::path& path) {loading.importing=true;loading.model.request(std::filesystem::absolute(path));}
    void applyModel(std::shared_ptr<one::Model> model,bool importing) {
        if(importing) {
            checkpoint();uint64_t id=1;for(const auto& e:world.entities)id=std::max(id,e.id+1);
            one::Entity e;e.id=id;e.name=model->source.stem().string();e.asset=model->source.generic_string();e.model=std::move(model);
            e.transform.position=camera.position+camera.forward()*5.f;world.entities.push_back(std::move(e));selected=id;commitEdit();message("Imported model");
        }else {
            auto next=world;next.replaceAsset(model->source,model);
            checkpoint();auto edit=edits.commit(next);dirty|=edit.changed;requestCollision(std::move(next));message("Preparing replacement collision");
        }
    }
    void pollLoads(Renderer& renderer) {
        loading.poll([&](std::shared_ptr<one::PhysicsScene> next){applyScene(std::move(next),renderer);},
            [&](auto model,bool importing){applyModel(std::move(model),importing);},
            [&](auto next){applyCollision(std::move(next));},
            [&](const auto& model){return std::any_of(world.entities.begin(),world.entities.end(),[&](const auto& e){return e.model==model;});},
            [&](std::string error){message("Load failed: "+error);});
    }
    void history(bool back,Renderer& renderer) {
        (void)renderer;endGesture();if(edits.pending())commitEdit();
        auto result=back?edits.undo(world):edits.redo(world);
        if(!result.changed)return;
        dirty=true;
        if(result.collision){loading.collision.cancel();rebuild();}
    }
    void save() { std::filesystem::create_directories(savePath.parent_path()); world.save(savePath);dirty=false;message("Saved "+savePath.string()); }
    void saveGame() {
        std::filesystem::create_directories("saves");world.save("saves/game.one");auto p=physics->playerPosition();
        nlohmann::json j={{"player",{p.x,p.y,p.z}},{"time",time},{"yaw",camera.yaw},{"pitch",camera.pitch},{"walk",walk},{"camera",{camera.position.x,camera.position.y,camera.position.z}}};
        j["version"]=1;auto velocity=physics->playerVelocity();j["playerVelocity"]={velocity.x,velocity.y,velocity.z};j["bodies"]=nlohmann::json::array();
        for(const auto& motion:physics->motions())j["bodies"].push_back({{"id",motion.id},{"linear",{motion.linear.x,motion.linear.y,motion.linear.z}},{"angular",{motion.angular.x,motion.angular.y,motion.angular.z}}});
        std::ofstream out("saves/game.json");out<<j.dump(2);if(!out)throw std::runtime_error("Cannot write gameplay save");message("Gameplay saved");
    }
    void loadGame(Renderer& renderer) {
        std::ifstream input("saves/game.json");nlohmann::json j;input>>j;
        if(j.at("version")!=1)throw std::runtime_error("Unsupported gameplay save version");
        auto vector=[](const nlohmann::json& a){
            if(!a.is_array() || a.size()!=3)throw std::runtime_error("Invalid saved vector");
            glm::vec3 v(a[0].get<float>(),a[1].get<float>(),a[2].get<float>());
            for(int i=0;i<3;++i)if(!std::isfinite(v[i]))throw std::runtime_error("Non-finite saved vector");return v;
        };
        auto position=vector(j.at("player")),velocity=vector(j.at("playerVelocity")),cameraPosition=vector(j.at("camera"));
        float savedTime=j.at("time").get<float>(),yaw=j.at("yaw").get<float>(),pitch=j.at("pitch").get<float>();bool savedWalk=j.at("walk").get<bool>();
        if(!std::isfinite(savedTime) || savedTime<0 || !std::isfinite(yaw) || !std::isfinite(pitch))throw std::runtime_error("Invalid saved clock/camera");
        std::vector<one::BodyMotion> motions;
        if(j.at("bodies").size()>8192)throw std::runtime_error("Saved body limit exceeded");
        for(const auto& body:j.at("bodies"))motions.push_back({body.at("id").get<uint64_t>(),vector(body.at("linear")),vector(body.at("angular"))});
        load("saves/game.one",renderer);physics->resetPlayer(position);physics->setPlayerVelocity(velocity);physics->restoreMotions(motions);
        time=savedTime;camera.position=cameraPosition;camera.yaw=yaw;camera.pitch=std::clamp(pitch,-1.5f,1.5f);walk=savedWalk;message("Gameplay loaded");
    }
};
void theme() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=7;s.FrameRounding=4;s.GrabRounding=4;s.WindowPadding={14,12};
    s.Colors[ImGuiCol_WindowBg]={.045f,.06f,.085f,.96f};s.Colors[ImGuiCol_Header]={.12f,.28f,.34f,1};s.Colors[ImGuiCol_Button]={.10f,.29f,.34f,1};s.Colors[ImGuiCol_CheckMark]={.2f,.85f,.75f,1};
}
void editor(Session& s,Renderer& renderer) {
    auto action=[&](const std::function<void()>& f){try{f();}catch(const std::exception& e){s.message(std::string("Error: ")+e.what());}};
    if(ImGui::BeginMainMenuBar()) {
        ImGui::TextColored({.25f,.9f,.8f,1},"THE ONE");ImGui::SameLine();ImGui::TextDisabled("IMPORT SANDBOX");ImGui::SameLine();
        if(ImGui::Button(s.simulate?"Pause":"Simulate"))s.simulate=!s.simulate;ImGui::SameLine();
        if(ImGui::Button(s.walk?"Fly camera [Tab]":"Walk / play [Tab]")){s.walk=!s.walk;if(s.walk)s.simulate=true;}ImGui::SameLine();
        if(ImGui::Button("Save scene"))action([&]{s.save();});ImGui::SameLine();
        if(ImGui::Button("Load saved"))action([&]{s.requestScene(s.savePath);});ImGui::SameLine();
        if(ImGui::Button("Undo"))action([&]{s.history(true,renderer);});ImGui::SameLine();
        if(ImGui::Button("Redo"))action([&]{s.history(false,renderer);});ImGui::SameLine();
        if(ImGui::Button("Profiler [F2]"))s.showProfiler=!s.showProfiler;ImGui::SameLine();
        ImGui::TextDisabled("%.1f fps | %.2f ms",ImGui::GetIO().Framerate,1000.0f/std::max(ImGui::GetIO().Framerate,1.0f));
        ImGui::EndMainMenuBar();
    }
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({12,44},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({280,430},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Scene hierarchy")) {
        ImGui::TextDisabled("%zu entities | %zu triangles",s.world.entities.size(),(s.renderScene.vertexCount+s.vertices.size())/3);ImGui::Separator();
        std::function<void(uint64_t)> tree=[&](uint64_t parent) {
            for(auto& e:s.world.entities)if(e.parent==parent) {
                bool children=false;for(const auto& other:s.world.entities)if(other.parent==e.id)children=true;
                auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|(children?0:ImGuiTreeNodeFlags_Leaf)|(s.selected==e.id?ImGuiTreeNodeFlags_Selected:0);
                bool open=ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id)),flags,"%s",e.name.c_str());
                if(ImGui::IsItemClicked())s.selected=e.id;
                if(open){tree(e.id);ImGui::TreePop();}
            }
        };tree(0);
        ImGui::Separator();ImGui::TextWrapped("Click an entity or click geometry in the viewport to select it.");
    }ImGui::End();
    ImGui::SetNextWindowPos({size.x-320,44},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({308,500},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Inspector")) {
        auto* e=s.world.find(s.selected);
        if(e) {
            ImGui::TextColored({.25f,.9f,.8f,1},"%s",e->name.c_str());ImGui::TextDisabled("Stable ID: %llu",static_cast<unsigned long long>(e->id));
            bool beforeVisible=e->visible;if(ImGui::Checkbox("Visible",&beforeVisible)){s.checkpoint();e->visible=beforeVisible;}
            // One transaction owns the whole gesture; only a Transform is copied per widget.
            auto before=e->transform;
            ImGui::DragFloat3("Position",glm::value_ptr(e->transform.position),.05f);
            if(ImGui::IsItemActivated())s.beginGesture(*e,before);
            if(ImGui::IsItemDeactivated())s.endGesture();
            auto angles=glm::degrees(glm::eulerAngles(e->transform.rotation));before=e->transform;
            if(ImGui::DragFloat3("Rotation",glm::value_ptr(angles),.5f))e->transform.rotation=glm::quat(glm::radians(angles));
            if(ImGui::IsItemActivated())s.beginGesture(*e,before);
            if(ImGui::IsItemDeactivated())s.endGesture();
            before=e->transform;
            ImGui::DragFloat3("Scale",glm::value_ptr(e->transform.scale),.01f,.01f,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::IsItemActivated())s.beginGesture(*e,before);
            if(ImGui::IsItemDeactivated())s.endGesture();
            std::string parentName=e->parent && s.world.find(e->parent)?s.world.find(e->parent)->name:"Scene root";
            if(ImGui::BeginCombo("Parent",parentName.c_str())) {
                auto parent=[&](uint64_t id,const char* label){if(ImGui::Selectable(label,e->parent==id)){
                    auto previous=e->parent;s.checkpoint();e->parent=id;
                    try{if(id && e->body!="none")throw std::runtime_error("Physics entities must be roots");s.world.matrix(e->id);}
                    catch(const std::exception& error){e->parent=previous;s.message(error.what());}
                }};
                parent(0,"Scene root");for(const auto& other:s.world.entities)if(other.id!=e->id)parent(other.id,other.name.c_str());ImGui::EndCombo();
            }
            ImGui::TextWrapped("Asset: %s",e->asset.empty()?"Transform only":e->asset.c_str());
            if(e->model) {
                if(ImGui::Button("Reload asset")){action([&]{s.reloadSelectedAsset();});e=s.world.find(s.selected);}
                if(!e->model->images.empty() && ImGui::TreeNode("Reload texture")) {
                    static int imageIndex=0; static char texturePath[1024]="";
                    imageIndex=std::clamp(imageIndex,0,static_cast<int>(e->model->images.size())-1);
                    ImGui::SliderInt("Image",&imageIndex,0,static_cast<int>(e->model->images.size())-1);
                    ImGui::InputTextWithHint("##texture","Image file path",texturePath,sizeof(texturePath));
                    if(ImGui::Button("Reload image"))action([&]{s.loading.imageOwner=e->model;s.loading.imageIndex=imageIndex;s.loading.image.request(texturePath);});
                    ImGui::TreePop();
                }
                bool anim=e->animate;if(ImGui::Checkbox("Animate",&anim)){s.checkpoint();e->animate=anim;}
                if(!e->model->animations.empty() && ImGui::BeginCombo("Clip",e->model->animations[std::clamp(e->clip,0,static_cast<int>(e->model->animations.size())-1)].name.c_str())) {
                    for(int i=0;i<static_cast<int>(e->model->animations.size());++i)if(ImGui::Selectable(e->model->animations[i].name.c_str(),e->clip==i)){s.checkpoint();e->clip=i;}ImGui::EndCombo();
                }
                if(ImGui::TreeNode("Imported nodes / rig")){for(const auto& n:e->model->nodes)ImGui::BulletText("%s%s",n.name.c_str(),n.skin>=0?" [skin]":"");ImGui::TreePop();}
                if(ImGui::TreeNode("Import diagnostics")){for(const auto& d:e->model->diagnostics)ImGui::TextWrapped("%s",d.c_str());ImGui::TreePop();}
            }
            ImGui::Separator();
            if(ImGui::BeginCombo("Physics",e->body.c_str())){for(const char* body:{"none","static","dynamic"})if(ImGui::Selectable(body,e->body==body))action([&]{if(e->parent && std::string(body)!="none")throw std::runtime_error("Physics bodies must be scene roots");s.checkpoint();e->body=body;s.simulate=false;});ImGui::EndCombo();}
            if(e->body=="dynamic" && ImGui::Button("Apply upward impulse"))s.physics->impulse(e->id,{0,12,0});
            if(ImGui::Button("Duplicate"))action([&]{auto copy=*e;s.checkpoint();copy.id=1;for(const auto& other:s.world.entities)copy.id=std::max(copy.id,other.id+1);copy.name+=" copy";copy.transform.position.x+=1.5f;s.selected=copy.id;s.world.entities.push_back(copy);});
        }else ImGui::TextDisabled("Select an entity");
    }ImGui::End();
    ImGui::SetNextWindowPos({12,size.y-210},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({620,198},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Assets and diagnostics")) {
        static char importPath[1024]="";ImGui::InputTextWithHint("##asset","Path to .glb or .gltf",importPath,sizeof(importPath));ImGui::SameLine();if(ImGui::Button("Import"))action([&]{s.import(importPath);});
        ImGui::TextDisabled("Or drop a glTF/GLB file onto the window.");
        ImGui::Checkbox("Shadows",&s.shadows);ImGui::SameLine();ImGui::Checkbox("Animations",&s.animation);ImGui::SameLine();if(ImGui::Button("Play sound"))s.soundEffect();
        ImGui::Separator();ImGui::TextWrapped("%s",s.status.c_str());
        if(ImGui::BeginChild("log",{0,65})){for(const auto& text:s.log)ImGui::TextWrapped("%s",text.c_str());}ImGui::EndChild();
    }ImGui::End();
    if(!s.gesture)s.commitEdit();
}
void pick(Session& s,float x,float y,float width,float height) {
    s.world.prepare(s.animation?s.time:0,s.renderScene);
    auto inverse=glm::inverse(s.camera.projection(width/height)*s.camera.view());
    auto rayEnd=inverse*glm::vec4(x/width*2-1,y/height*2-1,1,1);
    auto direction=glm::normalize(glm::vec3(rayEnd)/rayEnd.w-s.camera.position);
    s.selected=s.picker.pick(s.renderScene,s.camera.position,direction).entity;
}
}
int runApplication(const Options& options) {
    const bool repeatProfileReload = std::getenv("THEONE_PROFILE_RELOAD_REPEAT") != nullptr;
    FrameBoundary boundary;
    VisibleProfile profile(options.visibleProfile,options.profileProbe ? 240 : options.profileFrames,options.profileProbe);
    if(options.smoke || profile.enabled())SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD))throw std::runtime_error(SDL_GetError());
    struct Quit { ~Quit(){SDL_Quit();} } quit;
    auto* window=SDL_CreateWindow(options.robots?"Robots | TheOne":"Import Sandbox | TheOne",profile.enabled()?1920:1440,profile.enabled()?1080:900,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!window)throw std::runtime_error(SDL_GetError());
    struct Window { SDL_Window* w;~Window(){SDL_DestroyWindow(w);} } windowGuard{window};
    IMGUI_CHECKVERSION();ImGui::CreateContext();theme();ImGui::GetIO().IniFilename=nullptr;
    if(!ImGui_ImplSDL3_InitForVulkan(window))throw std::runtime_error("Cannot initialize UI platform");
    struct UI {~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}} ui;
    Renderer renderer;if(options.profileUncapped)renderer.setVsync(false);
    if(options.profileOneFrame)renderer.setFramesInFlight(1);
    renderer.initialize(window,options.validate);
    one::InputBindings bindings(std::filesystem::path(SDL_GetBasePath())/"assets/input.json");
    Session s;if(options.robots)s.respawn={options.spawnX,options.spawnY,options.spawnZ};auto scene=options.scene.empty()?std::filesystem::path(SDL_GetBasePath())/"assets/sandbox.one":options.scene;if(options.smoke)s.load(scene,renderer);else{s.rebuildImmediate();s.requestScene(scene);}s.selected=2;
    if(options.robots){s.walk=true;s.showEditor=false;s.camera.yaw=-1.57f;s.camera.pitch=-.20f;auto spawn=glm::vec3(options.spawnX,options.spawnY,options.spawnZ);s.physics->setRespawnPosition(spawn);s.physics->resetPlayer(spawn);}
    SDL_AudioSpec spec{};Uint8* wav=nullptr;Uint32 length=0;
    if(SDL_LoadWAV((std::filesystem::path(SDL_GetBasePath())/"assets/interact.wav").string().c_str(),&spec,&wav,&length)) {
        s.sound.assign(wav,wav+length);SDL_free(wav);s.audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(s.audio)SDL_ResumeAudioStreamDevice(s.audio);else s.message(std::string("Audio unavailable: ")+SDL_GetError());
    }else s.message(std::string("Audio file unavailable: ")+SDL_GetError());
    if(options.smoke && !s.audio)throw std::runtime_error("Smoke test audio stream creation failed");
    if(!options.music.empty()) {
        SDL_AudioSpec musicSpec{}; Uint8* data=nullptr; Uint32 bytes=0;
        if(SDL_LoadWAV(options.music.string().c_str(),&musicSpec,&data,&bytes)) {
            s.musicData.assign(data,data+bytes); SDL_free(data);
            s.music=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&musicSpec,nullptr,nullptr);
            if(s.music) { SDL_SetAudioStreamGain(s.music,.3f); SDL_ResumeAudioStreamDevice(s.music); }
            else s.message(std::string("Music device unavailable: ")+SDL_GetError());
        } else s.message(std::string("Music unavailable: ")+SDL_GetError());
        if(options.smoke && (!s.music || s.musicData.empty()))throw std::runtime_error("Smoke test music stream creation failed");
    }
    s.message(std::string("Audio driver: ")+SDL_GetCurrentAudioDriver());
    SDL_Gamepad* gamepad=nullptr;int gamepadCount=0;auto ids=SDL_GetGamepads(&gamepadCount);if(gamepadCount>0)gamepad=SDL_OpenGamepad(ids[0]);SDL_free(ids);
    struct Pad {SDL_Gamepad*& p;~Pad(){if(p)SDL_CloseGamepad(p);}} pad{gamepad};
    double bakeMilliseconds=0;unsigned bakeSamples=0;
    bool running=true;unsigned frames=0;Uint64 previous=SDL_GetTicksNS(),restoreAt=0;float jumpCooldown=0;
    bool testedIdle=false,testedMove=false,testedRise=false;
    s.message("Ready: Tab walk/fly; WASD move; RMB look; Space jump; F interact; F5 reload; F6/F9 save/load game");
    while(running) {
        profile.begin(!s.world.entities.empty() && !s.loading.busy() && !renderer.frameStats().pendingDraws);
        if(restoreAt && SDL_GetTicks()>restoreAt){SDL_RestoreWindow(window);restoreAt=0;}
        bool boundaryReady = boundary.begin(renderer, window);
        profile.sample("frame_boundary", profile.mark());
        if(s.music && !s.musicData.empty() && SDL_GetAudioStreamQueued(s.music)<static_cast<int>(s.musicData.size()/4)) {
            if(!SDL_PutAudioStreamData(s.music,s.musicData.data(),static_cast<int>(s.musicData.size()))) {
                if(options.smoke)throw std::runtime_error(std::string("Music queue failed: ")+SDL_GetError());
                s.message(std::string("Music queue failed: ")+SDL_GetError());
            }
        }
        float mouseX=0,mouseY=0;SDL_Event event;
        auto safe=[&](const std::function<void()>& action){try{action();}catch(const std::exception& e){if(options.smoke)throw;s.message(std::string("Error: ")+e.what());}};
        profile.pollInput();
        while(SDL_PollEvent(&event)) {
            // Keep the visible scripted replay independent of incidental desktop input.
            if(profile.enabled() && event.type!=SDL_EVENT_QUIT &&
               (event.type<SDL_EVENT_WINDOW_FIRST || event.type>SDL_EVENT_WINDOW_LAST))continue;
            ImGui_ImplSDL3_ProcessEvent(&event);
            if(event.type==SDL_EVENT_QUIT)running=false;
            if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type==SDL_EVENT_WINDOW_RESTORED)renderer.resize();
            if(event.type==SDL_EVENT_MOUSE_MOTION){mouseX+=event.motion.xrel;mouseY+=event.motion.yrel;}
            if(event.type==SDL_EVENT_DROP_FILE)safe([&]{s.import(event.drop.data);});
            if(event.type==SDL_EVENT_GAMEPAD_ADDED && !gamepad)gamepad=SDL_OpenGamepad(event.gdevice.which);
            if(event.type==SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad)==event.gdevice.which){SDL_CloseGamepad(gamepad);gamepad=nullptr;}
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && !ImGui::GetIO().WantCaptureKeyboard) {
                if(event.key.scancode==SDL_SCANCODE_F2)s.showProfiler=!s.showProfiler;
                if(event.key.scancode==bindings.key("interact")){auto services=s.services();s.game->interact(services);}
                switch(event.key.key) {
                    case SDLK_ESCAPE:running=false;break;
                    case SDLK_TAB:s.walk=!s.walk;if(s.walk)s.simulate=true;break;
                    case SDLK_F1:s.showEditor=!s.showEditor;break;
                    case SDLK_F5:safe([&]{s.requestScene(s.world.source.empty()?scene:s.world.source);s.doubleJump.reset();});break;
                    case SDLK_F6:safe([&]{s.saveGame();});break;
                    case SDLK_F9:safe([&]{s.loadGame(renderer);});break;
                    case SDLK_R:s.camera=one::Camera{};s.physics->resetPlayer(options.robots?glm::vec3(options.spawnX,options.spawnY,options.spawnZ):glm::vec3(0,2,10));s.doubleJump.reset();if(options.robots){s.camera.yaw=-1.57f;s.camera.pitch=-.20f;}break;
                    default:break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
                int w=0,h=0;SDL_GetWindowSize(window,&w,&h);if(w>0 && h>0)pick(s,event.button.x,event.button.y,static_cast<float>(w),static_cast<float>(h));
            }
        }
        if(!running)break;
        profile.sample("events",profile.mark());
        safe([&]{s.pollLoads(renderer);});
        profile.sample("load_apply",profile.mark());
        if(profile.enabled()) {
            auto n=profile.step();
            if(n==240) {
                s.showEditor=true;s.walk=false;s.simulate=false;
                for(const auto& e:s.world.entities)if(e.model && e.body=="static" && e.id!=1){s.selected=e.id;break;}
            }
            if(n==480) {
                auto e=s.world.find(s.selected);
                if(!e || e->body!="static")throw std::runtime_error("Visible profile lost its static edit target");
                s.beginGesture(*e,e->transform);
            }
            if(n>=480 && n<540)if(auto e=s.world.find(s.selected))e->transform.position.x+=.002f;
            if(n==540)s.endGesture();
            if(n==600)s.history(true,renderer);
            if(n==660)s.history(false,renderer);
            if(n==720 || (repeatProfileReload && (n==780 || n==840)))s.reloadSelectedAsset();
            if(n==900)s.requestScene(s.world.source);
        }
        profile.sample("action",profile.mark());
        if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED){SDL_Delay(10);previous=SDL_GetTicksNS();continue;}
        if(!boundaryReady){SDL_Delay(10);previous=SDL_GetTicksNS();continue;}
        auto now=SDL_GetTicksNS();float dt=std::min(static_cast<float>(now-previous)/1e9f,.2f);previous=now;
        // Replay camera movement must follow the same path regardless of frame waits.
        if(profile.enabled())dt=1.0f/60;
        bool focus=(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0;
        profile.inputSampled();
        const bool* keys=SDL_GetKeyboardState(nullptr);bool input=focus && !ImGui::GetIO().WantCaptureKeyboard && !options.smoke && !profile.enabled();
        auto down=[&](SDL_Scancode key){return input && keys[key];};
        bool looking=focus && !ImGui::GetIO().WantCaptureMouse && (SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_RMASK) && !options.smoke && !profile.enabled();
        SDL_SetWindowRelativeMouseMode(window,looking);
        if(looking){s.camera.yaw-=mouseX*.003f;s.camera.pitch-=mouseY*.003f;}
        if(down(SDL_SCANCODE_LEFT))s.camera.yaw+=1.7f*dt;if(down(SDL_SCANCODE_RIGHT))s.camera.yaw-=1.7f*dt;
        if(down(SDL_SCANCODE_UP))s.camera.pitch+=1.3f*dt;if(down(SDL_SCANCODE_DOWN))s.camera.pitch-=1.3f*dt;
        float padX=0,padY=0;
        if(gamepad && input) {
            auto axis=[&](SDL_GamepadAxis a){float value=SDL_GetGamepadAxis(gamepad,a)/32767.0f;return std::abs(value)>.15f?value:0.0f;};
            padX=axis(SDL_GAMEPAD_AXIS_LEFTX);padY=-axis(SDL_GAMEPAD_AXIS_LEFTY);s.camera.yaw-=axis(SDL_GAMEPAD_AXIS_RIGHTX)*dt*2;s.camera.pitch-=axis(SDL_GAMEPAD_AXIS_RIGHTY)*dt*2;
        }
        s.camera.pitch=std::clamp(s.camera.pitch,-1.5f,1.5f);
        auto forward=s.camera.forward(),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
        glm::vec3 movement=forward*(float(down(bindings.key("forward")))-float(down(bindings.key("back")))+padY)+right*(float(down(bindings.key("right")))-float(down(bindings.key("left")))+padX);
        if(options.smoke && options.robots && frames>=15 && frames<50) movement={1,0,0};
        if(profile.enabled() && profile.step()>=0 && profile.step()<240) {
            movement={(profile.step()%120)<60?1.f:-1.f,0,0};
            s.camera.yaw=3.14159265f+.15f*std::sin(float(profile.step())*.025f);
            s.camera.pitch=-.2f;
        }
        if(s.walk)movement.y=0;else movement.y+=float(down(bindings.key("ascend")))-float(down(bindings.key("descend")));
        if(glm::length(movement)>1)movement=glm::normalize(movement);
        float speed=down(bindings.key("fast"))?8.0f:4.5f;
        if(!s.walk && !options.smoke)s.camera.position+=movement*speed*dt;
        s.desiredMovement=s.walk?movement*speed:glm::vec3(0);
        jumpCooldown=std::max(0.0f,jumpCooldown-dt);
        bool jumping=down(bindings.key("jump")) || (input && gamepad && SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH));
        if(options.smoke && options.robots && frames==50 && s.physics->grounded()) jumping=true;
        if(options.robots) {
            if(s.doubleJump.update(jumping && s.walk,s.physics->grounded(),s.physics->playerVelocity().y)) {s.jump=true;s.soundEffect();}
        } else if(jumping && jumpCooldown==0 && s.physics->grounded()){s.jump=true;jumpCooldown=.3f;s.soundEffect();}
        float simulationDt=(options.smoke || profile.enabled())?1.0f/60:dt;
        profile.sample("input",profile.mark());
        int fixedSteps=0;
        if(s.simulate && s.loading.collision.status().state!=one::LoadState::Queued && s.loading.collision.status().state!=one::LoadState::Loading){s.accumulator+=simulationDt;while(s.accumulator>=1.0f/60){++fixedSteps;s.physics->step(s.world,1.0f/60,s.desiredMovement,s.jump,options.robots);s.jump=false;auto services=s.services();s.game->fixedUpdate(services,1.0f/60);s.effects.step(1.0f/60);s.time+=1.0f/60;s.accumulator-=1.0f/60;}}
        profile.sample("simulation",profile.mark());profile.sample("fixed_steps",fixedSteps);
        if(s.walk) {
            auto player=s.physics->playerPosition();
            if(options.robots) {
                if(auto* rodney=s.world.find(1000)) {
                    rodney->transform.position=player-glm::vec3(0,.8f,0);
                    if(glm::length(glm::vec2(movement.x,movement.z))>.01f)
                        rodney->transform.rotation=glm::angleAxis(std::atan2(movement.x,movement.z),glm::vec3(0,1,0));
                    if(rodney->model && !rodney->model->animations.empty()) {
                        // Candidate motion references recovered from the original mode scripts.
                        const char* wanted=one::robotsAnimation(s.physics->grounded(),s.physics->playerVelocity().y,glm::length(glm::vec2(movement.x,movement.z)));
                        if(options.smoke) {testedIdle|=std::string(wanted)=="original_0300000e";testedMove|=std::string(wanted)=="original_8300007b";testedRise|=std::string(wanted)=="original_8300006f";}
                        bool found=false;
                        for(size_t i=0;i<rodney->model->animations.size();++i)
                            if(rodney->model->animations[i].name==wanted){
                                if(s.robotAnimation!=wanted) {
                                    rodney->previousClip=s.robotAnimation.empty()?-1:rodney->clip;
                                    rodney->previousAnimationTime=s.robotAnimationTime;
                                    rodney->previousAnimationLoop=rodney->animationLoop;
                                    rodney->animationBlend=rodney->previousClip<0?1.0f:0.0f;
                                    s.robotAnimation=wanted;s.robotAnimationTime=0;
                                }
                                rodney->clip=static_cast<int>(i);found=true;break;
                            }
                        if(options.smoke && !found)throw std::runtime_error(std::string("Missing Rodney motion: ")+wanted);
                        rodney->animationLoop=s.physics->grounded();
                        if(s.simulate) {
                            s.robotAnimationTime+=simulationDt;
                            rodney->previousAnimationTime+=simulationDt;
                            rodney->animationBlend=std::min(1.0f,rodney->animationBlend+simulationDt/.15f);
                        }
                        rodney->animationTime=s.robotAnimationTime;
                    }
                }
                s.camera.position=player+glm::vec3(0,.9f,0)-s.camera.forward()*5.5f;
            } else s.camera.position=player+glm::vec3(0,.6f,0);
        }
        profile.sample("controller",profile.mark());
        if(!renderer.beginFrame()){SDL_Delay(10);continue;}
        profile.sample("begin_frame",profile.mark());
        ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();s.loading.draw();if(s.showProfiler)s.handoff.draw(*s.physics);if(s.showEditor)editor(s,renderer);if(s.showProfiler)renderer.drawProfiler(&s.showProfiler);
        profile.sample("editor",profile.mark());
        auto state=s.game->state();ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x*.5f,42},ImGuiCond_Always,{.5f,0});
        ImGui::SetNextWindowBgAlpha(.75f);if(ImGui::Begin("Game HUD",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs)) {
            if(options.robots) { ImGui::Text("Pickups: %d / %d",state.collected,state.total); ImGui::TextDisabled("WASD move | Right mouse look | Space double jump | R reset"); }
            else { ImGui::Text("Collectibles: %d / %d%s",state.collected,state.total,state.completed?"   COMPLETE":"");ImGui::TextDisabled("%s | F1 editor | Space jump | F interact",s.walk?"WALK":"FLY"); }
        }ImGui::End();
        if(renderer.frameStats().pendingDraws) {ImGui::Begin("Preparing graphics",nullptr,ImGuiWindowFlags_AlwaysAutoResize);ImGui::Text("%zu draws waiting for upload",renderer.frameStats().pendingDraws);ImGui::End();}
        ImGui::Render();
        profile.sample("hud",profile.mark());
        auto bakeStart=SDL_GetTicksNS();
        s.world.prepare(s.animation?s.time:0,s.renderScene);
        s.handoff.prepared(s.renderScene.preparation.cpuMs);
        s.vertices.clear();s.draws.clear();
        s.effects.bake(s.camera,s.vertices,s.draws);
        bakeMilliseconds+=static_cast<double>(SDL_GetTicksNS()-bakeStart)/1e6;++bakeSamples;
        profile.sample("scene_prepare",profile.mark());
        auto renderStart=std::chrono::steady_clock::now();
        if(renderer.render(s.renderScene,s.vertices,s.draws,s.camera,s.time,options.smoke && frames==110,s.shadows,s.selected,options.robots))++frames;
        s.handoff.rendered(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count(),renderer.frameStats().pendingDraws);
        profile.sample("render",profile.mark());
        profile.sample("reload_models_reused",double(s.handoff.stats.modelsReused));
        profile.sample("reload_models_replaced",double(s.handoff.stats.modelsReplaced));
        profile.sample("pooled_staging_bytes",double(renderer.assetMemoryStats().pooledStagingBytes));
        profile.sample("retired_bytes",double(renderer.assetMemoryStats().retiredGpuBytes));
        profile.sample("collision_requests",double(s.collisionRequests));
        profile.sample("undo_entities",double(s.edits.storedEntities()));
        profile.sample("focused", (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0);
        profile.end(renderer.frameStats(),renderer.assetMemoryStats().gpuTextureBytes,s.loading.busy(),
            !(SDL_GetWindowFlags(window)&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED)));
        if(profile.done()) {
            if(!options.profileProbe && s.collisionRequests!=(repeatProfileReload ? 6 : 4))throw std::runtime_error("Visible profile collision sequence was interrupted");
            running=false;
        }
        if(options.smoke) {
            if(frames==25)SDL_SetWindowSize(window,1200,800);
            if(frames==40){SDL_MinimizeWindow(window);restoreAt=SDL_GetTicks()+100;}
            if(frames==60){SDL_SetWindowSize(window,1440,900);s.save();}
            if(frames==70)s.load(s.savePath,renderer);
            if(frames==75)s.saveGame();
            if(frames==85)s.loadGame(renderer);
            if(frames==90)s.soundEffect();
            if(frames>=120){
                if(options.robots && (!testedIdle || !testedMove || !testedRise))throw std::runtime_error("Rodney idle/move/jump animation selection was not exercised");
                s.message("Smoke passed: 120 frames, texture/skin/shadow rendering, physics, save/reload, audio submission, resize and minimize/restore");running=false;
            }
        }
    }
    if(options.smoke)std::cout<<"Average CPU pose/effect preparation: "<<bakeMilliseconds/std::max(bakeSamples,1u)<<" ms (small compatibility scene only)"<<std::endl;
    profile.finish();
    SDL_SetWindowRelativeMouseMode(window,false);renderer.close();
    if(renderer.errors())throw std::runtime_error(std::to_string(renderer.errors())+" Vulkan validation errors");
    return 0;
}

