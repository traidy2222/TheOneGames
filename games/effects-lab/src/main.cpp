#include <theone/render/renderer.hpp>
#include <theone/volumes/volume.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace one;
namespace {
struct Lab {
    VolumeScene scene;
    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
    uint64_t meshRevision = 0;
    Material floor, wall, marker;
    int scenario = 0;
    uint32_t quality = 48;
    float fuel = .18f;
    void box(glm::vec3 lo, glm::vec3 hi, const Material* material, uint64_t id) {
        uint32_t start = uint32_t(vertices.size());
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side) {
                int a = (axis + 1) % 3, b = (axis + 2) % 3;
                glm::vec3 n(0);
                n[axis] = side ? 1.f : -1.f;
                std::array<glm::vec3, 4> p;
                for (int k = 0; k < 4; ++k) {
                    p[k] = lo;
                    p[k][axis] = side ? hi[axis] : lo[axis];
                    p[k][a] = (k == 1 || k == 2) ? hi[a] : lo[a];
                    p[k][b] = k >= 2 ? hi[b] : lo[b];
                }
                for (int k :
                     (side ? std::array<int, 6>{0, 1, 2, 0, 2, 3} : std::array<int, 6>{0, 2, 1, 0, 3, 2}))
                    vertices.push_back({p[k], n, glm::vec4(1), {float(k == 1 || k == 2), float(k >= 2)}});
            }
        draws.push_back({start, uint32_t(vertices.size()) - start, material, nullptr, id});
    }
    void reset() {
        auto generation = scene.generation + 1;
        scene = {};
        scene.generation = generation;
        scene.domain.origin = {-6, 0, -6};
        scene.domain.extent = {12, 18, 12};
        scene.domain.resolution = {quality, quality * 3 / 2, quality};
        scene.domain.maximumSubsteps = 24;
        scene.domain.initialTurbulenceRms = .8f;
        scene.domain.initialTurbulenceScale = 1.5f;
        scene.domain.pressureIterations = 48;
        scene.domain.memoryBudget = 768ull * 1024 * 1024;
        vertices.clear();
        draws.clear();
        ++meshRevision;
        floor.name = "Concrete";
        floor.color = {.3f, .32f, .34f, 1};
        floor.roughness = .9f;
        wall.color = {.36f, .38f, .4f, 1};
        wall.roughness = .8f;
        marker.color = {.72f, .12f, .035f, 1};
        marker.metallic = .5f;
        marker.roughness = .4f;
        box({-15, -.3f, -15}, {15, 0, 15}, &floor, 1);
        if (scenario == 1)
            scene.obstacles.push_back({{.8f, 0, -3}, {1.1f, 6, 3}, {0, 0, 0}});
        if (scenario == 2)
            scene.obstacles.push_back({{-4, 3.4f, -4}, {4, 3.65f, 4}, {0, 0, 0}});
        if (scenario == 3) {
            scene.domain.wind = {2.5f, 0, 0};
            scene.domain.extent.x = 24;
            scene.domain.resolution.x *= 2;
        }
        if (scenario == 5)
            scene.domain.gravity = {0, 0, 0};
        if (scenario == 4) {
            addFuelExplosion(scene, {-1.2f, 1.5f, 0}, fuel * .55f, 1.0f);
            addFuelExplosion(scene, {1.2f, 1.5f, 0}, fuel * .55f, 1.0f, .12f);
        } else
            addFuelExplosion(scene, {scenario == 1 ? -1.f : 0.f, 1.5f, 0}, fuel);
        for (size_t i = 0; i < scene.obstacles.size(); ++i)
            box(scene.obstacles[i].minimum, scene.obstacles[i].maximum, &wall, 2 + i);
        // Depth and lighting reference posts outside the fuel source.
        box({-2.8f, 0, 1.8f}, {-2.5f, 2, 2.1f}, &marker, 30);
        scene.obstacles.push_back({{-2.8f, 0, 1.8f}, {-2.5f, 2, 2.1f}, {0, 0, 0}});
    }
};
void lookAt(Camera& camera, glm::vec3 eye, glm::vec3 target) {
    camera.position = eye;
    auto d = glm::normalize(target - eye);
    camera.yaw = std::atan2(d.x, d.z);
    camera.pitch = std::asin(d.y);
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool validate = false, smoke = false, clean = false;
        int scenario = 0, framesLimit = 360, view = 0, lightPreset = 0;
        std::filesystem::path output = "build/captures";
        uint32_t quality = 48;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--validate")
                validate = true;
            else if (a == "--smoke-test")
                smoke = true;
            else if (a == "--clean")
                clean = true;
            else if (a == "--scenario" && i + 1 < argc)
                scenario = std::stoi(argv[++i]);
            else if (a == "--view" && i + 1 < argc)
                view = std::stoi(argv[++i]);
            else if (a == "--light" && i + 1 < argc)
                lightPreset = std::stoi(argv[++i]);
            else if (a == "--quality" && i + 1 < argc)
                quality = uint32_t(std::stoi(argv[++i]));
            else if (a == "--frames" && i + 1 < argc)
                framesLimit = std::stoi(argv[++i]);
            else if (a == "--output" && i + 1 < argc)
                output = argv[++i];
            else
                throw std::runtime_error("Unknown effects-lab option: " + a);
        }
        if (lightPreset < 0 || lightPreset > 3 || view < 0 || view > 3 || framesLimit < 1 || scenario < 0 ||
            scenario > 5 || quality < 16 || quality > 96)
            throw std::runtime_error("Scenario must be 0..5 and quality 16..96");
        std::filesystem::create_directories(output);
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("TheOne | Reactive smoke and fuel explosions", 1280, 720,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplSDL3_InitForVulkan(window))
            throw std::runtime_error("UI init failed");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        Renderer renderer;
        renderer.initialize(window, validate);
        renderer.setShadowDistance(45);
        renderer.setVsync(!smoke);
        SceneLighting light;
        light.sunDirection = glm::normalize(glm::vec3(-.5f, .6f, .45f));
        light.sunRadiance = {4, 3.8f, 3.6f};
        light.skyRadiance = {.18f, .23f, .32f};
        if (lightPreset == 1)
            light.sunDirection = glm::normalize(glm::vec3(.6f, .4f, -.5f));
        if (lightPreset == 2)
            light.sunDirection = glm::normalize(glm::vec3(.1f, .85f, .2f));
        if (lightPreset == 3) {
            light.sunRadiance = {.04f, .05f, .08f};
            light.skyRadiance = {.004f, .006f, .012f};
        }
        renderer.setLighting(light);
        Lab lab;
        lab.quality = quality;
        lab.scenario = scenario;
        lab.reset();
        Camera camera;
        lookAt(camera,
               view == 1   ? glm::vec3(-6, 4.5f, -9)
               : view == 2 ? glm::vec3(0, 1.8f, 0)
               : view == 3 ? glm::vec3(3.5f, 3.1f, 5.5f)
                           : glm::vec3(6, 4.5f, 9),
               view == 2 ? glm::vec3(0, 3, -6) : glm::vec3(0, 3, 0));
        bool running = true, paused = false;
        int frames = 0;
        uint64_t previous = SDL_GetTicksNS();
        std::ofstream trace(output / "frames.csv");
        trace << "frame,requested_time,simulated_time,debt,fuel_kg,soot_kg,burned_kg,max_kelvin,sim_ms,light_"
                 "ms,ray_ms,bytes,velocity_clamps,divergence\n";
        const char* names[] = {"Open fuel explosion", "Wall interaction",    "Ceiling interaction",
                               "Crosswind",           "Two merging sources", "Zero gravity"};
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_QUIT)
                    running = false;
                if (!smoke && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
                    !ImGui::GetIO().WantCaptureMouse)
                    SDL_SetWindowRelativeMouseMode(window, true);
                if ((e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) ||
                    e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    SDL_SetWindowRelativeMouseMode(window, false);
                if (!smoke && e.type == SDL_EVENT_MOUSE_MOTION &&
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_RELATIVE_MODE)) {
                    camera.yaw -= e.motion.xrel * .003f;
                    camera.pitch = std::clamp(camera.pitch - e.motion.yrel * .003f, -1.5f, 1.5f);
                }
                if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    renderer.resize();
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (e.key.key == SDLK_ESCAPE)
                        running = false;
                    if (e.key.key == SDLK_SPACE)
                        paused = !paused;
                    if (e.key.key == SDLK_R)
                        lab.reset();
                }
            }
            auto now = SDL_GetTicksNS();
            double dt = smoke ? 1. / 60. : std::min(.066, double(now - previous) / 1e9);
            previous = now;
            if (!smoke && !ImGui::GetIO().WantCaptureKeyboard) {
                auto keys = SDL_GetKeyboardState(nullptr);
                auto right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0, 1, 0)));
                auto move = camera.forward() * float(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) +
                            right * float(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) +
                            glm::vec3(0, 1, 0) * float(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]);
                if (glm::length(move) > .001f)
                    camera.position +=
                        glm::normalize(move) * float(dt) * (keys[SDL_SCANCODE_LSHIFT] ? 12.f : 4.f);
            }
            if (!paused)
                lab.scene.timeSeconds += dt;
            if (!renderer.beginFrame()) {
                SDL_Delay(10);
                continue;
            }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (!clean) {
                ImGui::SetNextWindowPos({15, 15}, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize({345, 0}, ImGuiCond_FirstUseEver);
                ImGui::Begin("Reactive volume lab");
                ImGui::TextUnformatted(names[lab.scenario]);
                if (ImGui::Combo("Scenario", &lab.scenario, names, 6))
                    lab.reset();
                if (ImGui::Button("Replay (R)"))
                    lab.reset();
                ImGui::SameLine();
                ImGui::Checkbox("Pause (Space)", &paused);
                ImGui::SliderFloat("Fuel mass (kg)", &lab.fuel, .03f, .6f);
                int q = int(lab.quality);
                if (ImGui::SliderInt("Grid width", &q, 16, 64)) {
                    lab.quality = uint32_t(q);
                    lab.reset();
                }
                ImGui::SliderFloat("Exposure", &light.exposure, .05f, 2);
                renderer.setLighting(light);
                auto s = renderer.volumeStats();
                ImGui::Separator();
                ImGui::Text("Sim %.3f s | debt %.3f s", s.simulatedSeconds, s.timeDebtSeconds);
                ImGui::Text("Fuel %.4f kg | soot %.5f kg", s.fuelKg, s.sootKg);
                ImGui::Text("Max %.0f K | %.2f m/s", s.maximumKelvin, s.maximumSpeed);
                ImGui::Text("GPU sim %.2f | light %.2f | ray %.2f ms", s.gpuSimulationMs, s.gpuLightingMs,
                            s.gpuRaymarchMs);
                ImGui::Text("Resources %.1f MiB | clamps %u", s.allocatedBytes / 1048576., s.velocityClamps);
                ImGui::TextWrapped(
                    "A low-Mach reactive gas simulation. Pressure shock waves, liquid fuel evaporation and "
                    "full spectral/global illumination are outside this model.");
                ImGui::End();
            }
            renderer.setVolumeScene(lab.scene);
            ImGui::Render();
            bool capture = smoke && (frames == 8 || frames == 20 || frames == 60 || frames == 150 ||
                                     frames == framesLimit - 1);
            if (renderer.renderCached(lab.vertices, lab.draws, lab.meshRevision, camera,
                                      float(lab.scene.timeSeconds), capture, true)) {
                auto s = renderer.volumeStats();
                trace << frames << ',' << lab.scene.timeSeconds << ',' << s.simulatedSeconds << ','
                      << s.timeDebtSeconds << ',' << s.fuelKg << ',' << s.sootKg << ',' << s.burnedFuelKg
                      << ',' << s.maximumKelvin << ',' << s.gpuSimulationMs << ',' << s.gpuLightingMs << ','
                      << s.gpuRaymarchMs << ',' << s.allocatedBytes << ',' << s.velocityClamps << ','
                      << s.maximumDivergenceError << '\n';
                if (capture)
                    std::filesystem::copy_file("capture.ppm",
                                               output / ("frame-" + std::to_string(frames) + ".ppm"),
                                               std::filesystem::copy_options::overwrite_existing);
                ++frames;
            }
            if (smoke && frames >= framesLimit)
                running = false;
        }
        auto snapshot = renderer.captureVolumeState();
        std::ofstream summary(output / "summary.txt");
        auto s = snapshot.stats;
        summary << "requested_seconds=" << lab.scene.timeSeconds
                << "\nsimulated_seconds=" << s.simulatedSeconds << "\nfuel_kg=" << s.fuelKg
                << "\nsoot_kg=" << s.sootKg << "\nburned_kg=" << s.burnedFuelKg
                << "\nmax_kelvin=" << s.maximumKelvin << "\nclamps=" << s.velocityClamps << "\n";
        renderer.close();
        if (renderer.errors())
            throw std::runtime_error("Volume lab reported Vulkan validation errors");
        std::cout << "PASS effects lab scenario=" << scenario << " simulated=" << s.simulatedSeconds
                  << " soot=" << s.sootKg << " max_kelvin=" << s.maximumKelvin << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Effects lab: " << e.what() << '\n';
        return 1;
    }
}
