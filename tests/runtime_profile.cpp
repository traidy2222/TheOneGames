#include "game.hpp"
#include <theone/scene/picking.hpp>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <iostream>
#include <map>
#include <theone/physics/edit.hpp>
#include <theone/physics/scene_loading.hpp>
#include <theone/render/renderer.hpp>
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
void require(bool v, const char *text) {
  if (!v)
    throw std::runtime_error(text);
}
struct Samples {
  std::map<std::string, std::vector<double>> values;
  void add(std::string name, double ms) { values[name].push_back(ms); }
  void print() {
    for (auto &[name, v] : values) {
      std::sort(v.begin(), v.end());
      double sum = 0;
      for (auto n : v)
        sum += n;
      std::cout << name << " mean=" << sum / v.size()
                << " p95=" << v[size_t((v.size() - 1) * .95)]
                << " p99=" << v[size_t((v.size() - 1) * .99)]
                << " max=" << v.back() << '\n';
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
#ifdef _WIN32
    require(SetThreadAffinityMask(GetCurrentThread(), 1) != 0,
            "Cannot pin profile thread");
    std::cout << "profile_main_thread_logical_cpu=0" << std::endl;
#endif
    require(argc >= 2, "Expected scene path");
    auto loaded = one::PhysicsScene::load(argv[1]);
    std::cout << "scene_import_ms=" << loaded->importMs
              << " collision_build_ms=" << loaded->collisionMs << std::endl;
    auto &world = loaded->world;
    auto &physics = *loaded->physics;
    auto game = one::createSandboxGame();
    one::GameServices services{world, physics, [] {}};
    game->start(services);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct Quit {
      ~Quit() { SDL_Quit(); }
    } quit;
    int width=640, height=480, frames=128;
    bool profilePicking=false;
    for(int i=2;i<argc;++i) {
      std::string_view arg(argv[i]);
      if(arg=="--1080p") { width=1920; height=1080; }
      if(arg.starts_with("--frames=")) frames=std::stoi(std::string(arg.substr(9)));
      if(arg=="--pick-profile") profilePicking=true;
    }
    require(frames>0 && frames<=36000, "Frame count out of range");
    std::cout << "profile_width="<<width<<" height="<<height<<" frames="<<frames<<std::endl;
    auto *window = SDL_CreateWindow("Bounded gameplay frame profile", width, height,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    require(window, SDL_GetError());
    struct Window {
      SDL_Window *p;
      ~Window() { SDL_DestroyWindow(p); }
    } wg{window};
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    require(ImGui_ImplSDL3_InitForVulkan(window), "UI init");
    struct UI {
      ~UI() {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
      }
    } ui;
    one::Renderer renderer;
    bool collisionEdit = false;
    for(int i=2;i<argc;++i){
      if(std::string_view(argv[i])=="--no-instancing")renderer.setInstancing(false);
      else if(std::string_view(argv[i])=="--no-streaming")renderer.setTextureStreaming(false);
      else if(std::string_view(argv[i])=="--uncapped")renderer.setVsync(false);
      else if(std::string_view(argv[i])=="--direct-shadow-batches")renderer.setShadowBatching(true, false);
      else if(std::string_view(argv[i])=="--no-shadow-batching")renderer.setShadowBatching(false);
      else if(std::string_view(argv[i])=="--no-spatial-culling")renderer.setSpatialCulling(false);
      else if(std::string_view(argv[i])=="--no-shadow-lod")renderer.setShadowLod(false);
      else if(std::string_view(argv[i])=="--collision-edit")collisionEdit=true;
      else if(std::string_view(argv[i])=="--1080p" || std::string_view(argv[i]).starts_with("--frames=") || std::string_view(argv[i])=="--pick-profile") {}
      else throw std::runtime_error("Unknown profile option");
    }
    renderer.initialize(window, false);
    std::cout << "presentation=" << (renderer.vsyncEnabled() ? "fifo" : "uncapped") << std::endl;
    one::Camera camera;
    camera.position = physics.playerPosition() + glm::vec3(0, 2, 7);
    camera.yaw = 3.14159265f;
    camera.pitch = -.2f;
    one::RenderScene scene;
    Samples warm, samples;
    auto render = [&](float time) {
      world.prepare(time, scene);
      SDL_PumpEvents();
      require(renderer.beginFrame(), "No frame");
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      ImGui::Render();
      require(renderer.render(scene, {}, {}, camera, time), "Render failed");
    };
    for (int f = 0; f < 1000; ++f) {
      render(0);
      warm.add("cold_render_prepare_ms", renderer.frameStats().prepareMs);
      if (!renderer.frameStats().pendingDraws &&
          !renderer.frameStats().textureTransitions)
        break;
      require(f < 999, "Upload timeout");
    }
    uint64_t previousGpu = 0;
    for (int f = 0; f < frames; ++f) {
      auto frame = Clock::now(), start = frame;
      float time = float(f) / 60;
      glm::vec3 movement =
          (f % 128) < 64 ? glm::vec3(1.5f, 0, 0) : glm::vec3(-1.5f, 0, 0);
      physics.step(world, 1.f / 60, movement, (f % 128) == 30, true);
      samples.add("physics_ms", elapsed(start));
      start = Clock::now();
      game->fixedUpdate(services, 1.f / 60);
      samples.add("game_ms", elapsed(start));
      start = Clock::now();
      if (auto *actor = world.find(1000)) {
        actor->transform.position =
            physics.playerPosition() - glm::vec3(0, .8f, 0);
        auto wanted = one::robotsAnimation(physics.grounded(),
                                           physics.playerVelocity().y, 1);
        for (size_t i = 0; i < actor->model->animations.size(); ++i)
          if (actor->model->animations[i].name == wanted) {
            actor->clip = int(i);
            break;
          }
        actor->animationTime = time;
        actor->animationLoop = physics.grounded();
      }
      samples.add("animation_controller_ms", elapsed(start));
      camera.position = physics.playerPosition() + glm::vec3(0, 2, 7);
      camera.yaw = 3.14159265f;
      camera.pitch = -.2f;
      render(time);
      const auto &s = renderer.frameStats();
      samples.add("scene_animation_prepare_ms", s.scenePrepareMs);
      samples.add("render_prepare_ms", s.prepareMs);
      samples.add("record_ms", s.recordMs);
      samples.add("renderer_cpu_ms", s.prepareMs + s.recordMs + s.memoryStatsMs);
      samples.add("assetSync_ms", s.assetSyncMs);
      samples.add("geometryPrepare_ms", s.geometryPrepareMs);
      samples.add("visibility_ms", s.visibilityMs);
      samples.add("materialPrepare_ms", s.materialPrepareMs);
      samples.add("drawList_ms", s.drawListMs);
      samples.add("uniformPalette_ms", s.uniformPaletteMs);
      samples.add("shadowRecord_ms", s.shadowRecordMs);
      samples.add("mainRecord_ms", s.mainRecordMs);
      samples.add("memoryStats_ms", s.memoryStatsMs);
      samples.add("rigid_bounds_reused", double(s.rigidBoundsReused));
      samples.add("bounds_evaluated", double(s.boundsEvaluated));
      samples.add("shadowCullSampleMs", double(s.shadowCullSampleMs));
      samples.add("shadowIssueSampleMs", double(s.shadowIssueSampleMs));
      samples.add("shadowPlanMs", double(s.shadowPlanMs));
      samples.add("shadowGeometryBinds", double(s.shadowGeometryBinds));
      samples.add("shadowCommands", double(s.shadowCommands));
      samples.add("shadowCandidates", double(s.shadowCandidates));
      samples.add("shadowTriangles", double(s.shadowTriangles));
      samples.add("shadowLodDraws", double(s.shadowLodDraws));
      samples.add("shadowTrianglesSaved", double(s.shadowTrianglesSaved));
      samples.add("regionTests", double(s.regionTests));
      samples.add("regionRejectedDraws", double(s.regionRejectedDraws));
      samples.add("assetOwnersChecked", double(s.assetOwnersChecked));
      samples.add("batch_ms", s.batchMs);
      samples.add("residency_ms", s.residencyMs);
      samples.add("residency_commit_ms",s.residencyCommitMs);
      samples.add("residency_demand_ms",s.residencyDemandMs);
      samples.add("residency_plan_ms",s.residencyPlanMs);
      samples.add("residency_bindings_ms",s.residencyBindingsMs);
      samples.add("material_creation_ms",renderer.assetUploadStats().materialTotalMs);
      samples.add("texture_bytes",double(renderer.assetMemoryStats().gpuTextureBytes));
      samples.add("retired_bytes",double(renderer.assetMemoryStats().retiredGpuBytes));
      samples.add("texture_transitions", double(s.textureTransitions));
      samples.add("texture_changes_queued",double(s.textureChangesQueued));
      samples.add("texture_changes_committed",double(s.textureChangesCommitted));
      samples.add("texture_promotions",double(s.texturePromotions));
      samples.add("texture_demotions",double(s.textureDemotions));
      samples.add("retirement_ms",s.retirementMs);
      samples.add("fence_ms", s.fenceWaitMs);
      samples.add("acquire_ms", s.acquireWaitMs);
      samples.add("present_ms", s.presentMs);
      samples.add("wall_ms", elapsed(frame));
      samples.add("main_draws", double(s.mainDraws));
      samples.add("main_instances", double(s.mainInstances));
      samples.add("pending_draws", double(s.pendingDraws));
      samples.add("upload_cpu_ms", s.uploadCpuMs);
      samples.add("shadow_draws", double(s.shadowDraws));
      if (s.gpuTimingAvailable && s.gpuFrame != previousGpu) {
        previousGpu = s.gpuFrame;
        samples.add("gpu_ms", s.gpuTotalMs);
        samples.add("gpu_main_ms", s.gpuMainMs);
        samples.add("gpu_shadow_ms", s.gpuShadowMs);
      }
    }
    if(profilePicking) {
      std::vector<one::Vertex> vertices; std::vector<one::Draw> draws;
      one::ScenePicker picker; one::RenderScene pickScene;
      uint64_t checksum=0;
      for(int i=0;i<25;++i) {
        auto start=Clock::now();
        world.bake(float(frames)/60, vertices, draws);
        auto inv=glm::inverse(camera.projection(float(width)/height)*camera.view());
        auto rayEnd=inv*glm::vec4((i%5-2)*.3f,(i/5-2)*.3f,1,1);
        auto direction=glm::normalize(glm::vec3(rayEnd)/rayEnd.w-camera.position);
        float nearest=std::numeric_limits<float>::max(); uint64_t selected=0;
        for(const auto& draw:draws) for(uint32_t j=0;j<draw.count;j+=3) {
          auto a=vertices[draw.first+j].position, b=vertices[draw.first+j+1].position, c=vertices[draw.first+j+2].position;
          auto e1=b-a,e2=c-a,h=glm::cross(direction,e2); float det=glm::dot(e1,h);
          if(std::abs(det)<1e-7f) continue;
          auto offset=camera.position-a; float u=glm::dot(offset,h)/det;
          if(u<0 || u>1)continue;
          auto q=glm::cross(offset,e1); float v=glm::dot(direction,q)/det;
          if(v<0 || u+v>1)continue;
          float t=glm::dot(e2,q)/det;
          if(t>0 && t<nearest){nearest=t;selected=draw.entity;}
        }
        samples.add("editor_pick_reference_ms",elapsed(start)); checksum+=selected;
        start=Clock::now();
        world.prepare(float(frames)/60,pickScene);
        auto picked=picker.pick(pickScene,camera.position,direction);
        samples.add("editor_pick_ms",elapsed(start));
        samples.add("editor_pick_candidates",double(picked.candidates));
        samples.add("editor_pick_triangles",double(picked.triangles));
        require(picked.entity==selected,"Accelerated picking differs from baked reference");
      }
      std::cout<<"pick_checksum="<<checksum<<std::endl;
    }
    warm.print();
    samples.print();
    auto memory = renderer.assetMemoryStats();
    std::cout << "texture_bytes=" << memory.gpuTextureBytes
              << " shadow_command_bytes=" << memory.shadowCommandBytes
              << " models=" << memory.models << '\n';
    renderer.close();
    require(renderer.errors() == 0, "Renderer error");
    if (!collisionEdit) return 0;
    auto desired = world;
    for (auto &entity : desired.entities)
      if (entity.body == "static") {
        entity.transform.position.x += .01f;
        break;
      }
    auto start = Clock::now();
    auto request = physics.captureEdit(desired);
    auto captureMs = elapsed(start);
    one::PhysicsEditJob editJob;
    editJob.request(std::move(request));
    std::shared_ptr<one::PhysicsEdit> edit;
    start = Clock::now();
    for (int n = 0; !edit; ++n) {
      edit = editJob.take();
      auto status = editJob.status();
      require(status.state != one::LoadState::Failed, status.error.c_str());
      require(n < 10000, "Edit worker timeout");
      if (!edit)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto prepareMs = elapsed(start);
    start = Clock::now();
    physics.applyEdit(*edit, world);
    auto applyMs = elapsed(start);
    std::cout << "collision_edit capture_ms=" << captureMs
              << " worker_wait_ms=" << prepareMs << " apply_ms=" << applyMs
              << " moved=" << edit->stats.moved
              << " replaced=" << edit->stats.replaced
              << " unchanged=" << edit->stats.unchanged << '\n';
    editJob.retire(std::move(edit));
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}



