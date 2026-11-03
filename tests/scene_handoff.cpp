#include "scene_handoff.hpp"
#include "background_loading.hpp"
#include "game.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <iostream>
#include <chrono>
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected scene");
        bool validate = argc < 3;
        auto initial = one::PhysicsScene::load(argv[1]);
        one::World world;
        std::unique_ptr<one::Physics> physics;
        initial->swapInto(world, physics);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("Scene handoff profile", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
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
        renderer.initialize(window, validate);
        one::Camera camera;
        one::RenderScene scene;
        SceneHandoff handoff;
        auto render = [&] {
            world.prepare(0, scene);
            handoff.prepared(scene.preparation.cpuMs);
            auto start = std::chrono::steady_clock::now();
            SDL_PumpEvents();
            require(renderer.beginFrame(), "No frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
            require(renderer.render(scene, {}, {}, camera, 0, false, true), "Render failed");
            handoff.rendered(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                renderer.frameStats().pendingDraws);
        };
        auto settle = [&] {
            for (int i = 0; i < 1000; ++i) {
                render();
                if (!renderer.frameStats().pendingDraws && !renderer.frameStats().textureTransitions)
                    return;
            }
            throw std::runtime_error("Upload timeout");
        };
        settle();
        auto next = one::PhysicsScene::load(argv[1]);
        std::vector<one::World> undo(32, world), redo(8, world);
        auto game = one::createSandboxGame();
        double reloadCollisionMs = next->collisionMs;
        BackgroundLoads retirement;
        handoff.apply(
            *next, world, physics, renderer, [&] { retirement.retireHistory(undo, redo); },
            [&] {
                one::GameServices services{world, *physics, [] {}};
                game->start(services);
            });
        retirement.scene.retire(std::move(next));
        settle();
        auto s = handoff.stats;
        require(s.modelsReused>0 && s.modelsReplaced==0,"Unchanged reload did not reuse assets");
        require(renderer.frameStats().pendingDraws==0 && renderer.frameStats().transferBytes==0,"Unchanged reload reuploaded resources");
        auto noChange=one::PhysicsEdit::prepare(physics->captureEdit(world));
        require(noChange->stats.replaced==0 && noChange->stats.added==0,"Reuse lost collision owner identity");
        // A live image edit is not the on-disk source and must not survive a source reload.
        auto edited=std::find_if(world.entities.begin(),world.entities.end(),[](const auto& e){return e.model && !e.model->images.empty();});
        require(edited!=world.entities.end(),"No image fixture");
        auto editedModel=edited->model;editedModel->images[0].rgba[0]^=255;++editedModel->images[0].revision;
        auto fresh=one::PhysicsScene::load(argv[1]);
        handoff.apply(*fresh,world,physics,renderer,[]{},[]{});
        require(handoff.stats.modelsReplaced==1 && handoff.stats.modelsReused>0,"Edited source was incorrectly reused");
        retirement.scene.retire(std::move(fresh));settle();
        auto cache = physics->collisionCacheStats();
        auto memory = renderer.assetMemoryStats();
        require(s.uploadFrames > 0 && undo.empty() && redo.empty(), "Incomplete handoff");
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation failed");
        std::cout << "HANDOFF total_ms=" << s.totalMs << " invalidation_ms=" << s.invalidateMs
                  << " history_ms=" << s.historyMs << " swap_ms=" << s.swapMs
                  << " game_callbacks_ms=" << s.callbacksMs << " first_prepare_ms=" << s.firstPrepareMs
                  << " first_render_ms=" << s.firstRenderMs << " upload_frames=" << s.uploadFrames << " models_reused="<<s.modelsReused << '\n';
        std::cout << "COLLISION initial_ms=" << initial->collisionMs
                  << " cached_reload_ms=" << reloadCollisionMs << '\n';
        std::cout << "CACHE hits=" << cache.hits << " cooks=" << cache.cooks << " bytes=" << cache.bytes
                  << " GPU geometry=" << memory.gpuGeometryBytes << " textures=" << memory.gpuTextureBytes
                  << " aliases=" << memory.textureAliases << '\n';
        std::cout << "PASS complete scene handoff, first frame and upload convergence\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
