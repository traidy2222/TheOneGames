#include <theone/render/renderer.hpp>
#include <theone/lighting/stellar.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace one;
namespace {
constexpr double unit = 1e6, bodyRadius = 6371000., pi = 3.141592653589793;
void need(bool yes, const char* text) {
    if (!yes)
        throw std::runtime_error(text);
}
void aim(Camera& c, glm::vec3 p, glm::vec3 target) {
    c.position = p;
    auto d = glm::normalize(target - p);
    c.yaw = std::atan2(d.x, d.z);
    c.pitch = std::asin(d.y);
}
void sphere(std::vector<Vertex>& vertices, std::vector<Draw>& draws, glm::vec3 centre, float radius,
            Material* material, uint64_t entity) {
    uint32_t start = uint32_t(vertices.size());
    constexpr int rows = 48, columns = 96;
    auto vertex = [&](int y, int x) {
        double latitude = -pi * .5 + pi * y / rows, longitude = 2 * pi * x / columns;
        glm::vec3 n{float(std::cos(latitude) * std::cos(longitude)), float(std::sin(latitude)),
                    float(std::cos(latitude) * std::sin(longitude))};
        float rock = 1;
        return Vertex{
            centre + n * radius, n, glm::vec4(rock, rock, rock, 1), {float(x) / columns, float(y) / rows}};
    };
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            auto a = vertex(y, x), b = vertex(y + 1, x), c = vertex(y + 1, x + 1), d = vertex(y, x + 1);
            for (auto v : {a, b, c, a, c, d})
                vertices.push_back(v);
        }
    draws.push_back({start, uint32_t(vertices.size()) - start, material, nullptr, entity});
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool validate = false, smoke = false, clean = false;
        int scenario = 0, framesLimit = 12;
        std::filesystem::path output = "build/captures";
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
            else if (a == "--frames" && i + 1 < argc)
                framesLimit = std::stoi(argv[++i]);
            else if (a == "--output" && i + 1 < argc)
                output = argv[++i];
            else
                throw std::runtime_error("Unknown stellar sandbox argument: " + a);
        }
        need(scenario >= 0 && scenario <= 5 && framesLimit > 0, "Invalid stellar sandbox scenario/frames");
        std::filesystem::create_directories(output);
        need(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("TheOne | Stellar sandbox", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        need(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        need(ImGui_ImplSDL3_InitForVulkan(window), "UI initialization failed");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        Renderer renderer;
        renderer.initialize(window, validate);
        renderer.setVsync(!smoke);
        SceneLighting lighting;
        lighting.sunRadiance = {0, 0, 0};
        lighting.skyRadiance = {0, 0, 0};
        StellarScene stars;
        stars.metresPerUnit = unit;
        stars.stars.push_back(StellarEmitter{});
        float distanceAU = .05f, kelvin = 5772, radiusSolar = 1, exposureStops = -14, offset = 0;
        bool automaticExposure = true, eclipse = false, litSide = false;
        if (scenario == 1)
            distanceAU = 1;
        if (scenario == 2) {
            eclipse = true;
            litSide = true;
        }
        if (scenario == 3) {
            eclipse = true;
            litSide = true;
            offset = .8f;
        }
        if (scenario == 4) {
            distanceAU = 206264.8f;
            litSide = true;
        }
        if (scenario == 5)
            litSide = true;
        Camera camera;
        camera.nearPlane = .02f;
        camera.farPlane = 2000;
        auto resetCamera = [&] {
            float r = float(bodyRadius / unit);
            aim(camera, litSide ? glm::vec3(-3 * r, .65f * r, 2 * r) : glm::vec3(3 * r, .65f * r, 2 * r),
                {0, 0, 0});
        };
        resetCamera();
        Material rock, occulter;
        rock.color = {.32f, .29f, .25f, 1};
        rock.roughness = .95f;
        occulter.color = {.22f, .24f, .27f, 1};
        occulter.roughness = 1;
        std::vector<Vertex> vertices;
        std::vector<Draw> draws;
        uint64_t revision = 0;
        float previousOffset = 100;
        bool previousEclipse = false;
        bool running = true;
        int frames = 0;
        uint64_t previous = SDL_GetTicksNS();
        std::ofstream metrics(output / "frames.csv");
        metrics << "frame,distance_au,flux_w_m2,visibility,exposure,frame_ms\n";
        while (running) {
            auto frameStart = SDL_GetTicksNS();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_QUIT)
                    running = false;
                if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    renderer.resize();
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)
                    running = false;
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_R)
                    resetCamera();
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
                    !ImGui::GetIO().WantCaptureMouse)
                    SDL_SetWindowRelativeMouseMode(window, true);
                if ((e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) ||
                    e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    SDL_SetWindowRelativeMouseMode(window, false);
                if (e.type == SDL_EVENT_MOUSE_MOTION &&
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_RELATIVE_MODE)) {
                    camera.yaw -= e.motion.xrel * .003f;
                    camera.pitch = std::clamp(camera.pitch - e.motion.yrel * .003f, -1.5f, 1.5f);
                }
            }
            auto now = SDL_GetTicksNS();
            double dt = std::min(.05, double(now - previous) / 1e9);
            previous = now;
            if (!smoke && !ImGui::GetIO().WantCaptureKeyboard) {
                auto keys = SDL_GetKeyboardState(nullptr);
                auto right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0, 1, 0)));
                auto move = camera.forward() * float(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) +
                            right * float(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) +
                            glm::vec3(0, 1, 0) * float(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]);
                if (glm::length(move) > .001f)
                    camera.position +=
                        glm::normalize(move) * float(dt) * (keys[SDL_SCANCODE_LSHIFT] ? 80.f : 8.f);
            }
            if (!renderer.beginFrame()) {
                SDL_Delay(5);
                continue;
            }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            bool faceStar = false;
            if (!clean) {
                ImGui::SetNextWindowPos({std::max(16.f, ImGui::GetIO().DisplaySize.x - 381.f), 16},
                                        ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize({365, 0}, ImGuiCond_FirstUseEver);
                ImGui::Begin("Stellar sandbox");
                ImGui::TextUnformatted("Finite star / dark space");
                ImGui::SliderFloat("Distance (AU)", &distanceAU, .01f, 1000.f, "%.5g",
                                   ImGuiSliderFlags_Logarithmic);
                if (ImGui::Button("Near star"))
                    distanceAU = .05f;
                ImGui::SameLine();
                if (ImGui::Button("1 AU"))
                    distanceAU = 1;
                ImGui::SameLine();
                if (ImGui::Button("1 parsec"))
                    distanceAU = 206264.8f;
                ImGui::SliderFloat("Surface K", &kelvin, 1000, 10000, "%.0f");
                ImGui::SliderFloat("Radius / Sun", &radiusSolar, .2f, 5);
                ImGui::Checkbox("Automatic exposure", &automaticExposure);
                ImGui::SliderFloat("Exposure stops", &exposureStops, -24, 4);
                if (ImGui::Button("Star + crescent")) {
                    litSide = false;
                    resetCamera();
                }
                ImGui::SameLine();
                if (ImGui::Button("Lit side")) {
                    litSide = true;
                    resetCamera();
                }
                if (ImGui::Button("Face star"))
                    faceStar = true;
                ImGui::Text("Frame rate %.1f fps", ImGui::GetIO().Framerate);
                ImGui::Checkbox("Eclipsing body", &eclipse);
                ImGui::SliderFloat("Occulter offset", &offset, -3, 3);
                ImGui::Separator();
            }
            distanceAU =
                std::max(distanceAU, float(solarRadiusMetres * radiusSolar * 1.25 / astronomicalUnitMetres));
            stars.originMetres = {double(distanceAU) * astronomicalUnitMetres, 0, 0};
            stars.stars[0].kelvin = kelvin;
            stars.stars[0].radiusMetres = solarRadiusMetres * radiusSolar;
            stars.occluders = {StellarOccluder{stars.originMetres, bodyRadius, 1}};
            glm::vec3 occulterLocal{float(-4 * bodyRadius / unit), 0, float(offset * bodyRadius / unit)};
            if (eclipse)
                stars.occluders.push_back(
                    {stars.originMetres + glm::dvec3(occulterLocal) * unit, bodyRadius * .55, 2});
            if (!revision || previousEclipse != eclipse || previousOffset != offset) {
                vertices.clear();
                draws.clear();
                sphere(vertices, draws, {0, 0, 0}, float(bodyRadius / unit), &rock, 1);
                if (eclipse)
                    sphere(vertices, draws, occulterLocal, float(bodyRadius * .55 / unit), &occulter, 2);
                previousEclipse = eclipse;
                previousOffset = offset;
                ++revision;
            }
            if (faceStar)
                aim(camera, camera.position,
                    glm::vec3((stars.stars[0].positionMetres - stars.originMetres) / unit));
            auto worldEye = stars.originMetres + glm::dvec3(camera.position) * unit;
            auto delta = worldEye - stars.stars[0].positionMetres;
            double exclusion = stars.stars[0].radiusMetres * 1.02;
            if (glm::length(delta) < exclusion) {
                worldEye = stars.stars[0].positionMetres + glm::normalize(delta) * exclusion;
                camera.position = glm::vec3((worldEye - stars.originMetres) / unit);
            }
            double flux = stellarIrradianceWattsPerM2(stars.stars[0], stars.originMetres);
            auto probe = stars.originMetres - glm::dvec3(bodyRadius * 1.01, 0, 0);
            double visible = stellarVisibility(stars, 0, probe, 1);
            lighting.exposure = std::clamp(std::exp2(exposureStops) *
                                               (automaticExposure ? std::pow(distanceAU / .05f, 2) : 1.),
                                           1e-12, 16.);
            if (!clean) {
                ImGui::Text("Luminosity %.4e W", stellarLuminosityWatts(stars.stars[0]));
                ImGui::Text("Unblocked flux %.6g W/m2", flux);
                ImGui::Text("Substellar visibility %.1f %%", visible * 100);
                ImGui::Text("Angular diameter %.6g deg",
                            stellarAngularRadius(stars.stars[0], worldEye) * 360 / pi);
                ImGui::Text("Exposure multiplier %.6g", lighting.exposure);
                ImGui::TextWrapped("Right mouse looks. WASD flies; Q/E moves vertically; Shift boosts; R "
                                   "resets camera. The observer is a survey camera.");
                ImGui::TextWrapped("Blackbody emitter, finite disk samples and spherical occultation. No "
                                   "atmosphere, dust, stellar corona or light-travel delay yet.");
                ImGui::End();
            }
            renderer.setLighting(lighting);
            renderer.setStellarScene(stars);
            ImGui::Render();
            bool capture = smoke && frames == framesLimit - 1;
            if (renderer.renderCached(vertices, draws, revision, camera, 0, capture, false)) {
                metrics << frames << ',' << distanceAU << ',' << flux << ',' << visible << ','
                        << lighting.exposure << ',' << double(SDL_GetTicksNS() - frameStart) / 1e6 << '\n';
                if (capture)
                    std::filesystem::copy_file("capture.ppm", output / "star.ppm",
                                               std::filesystem::copy_options::overwrite_existing);
                ++frames;
            }
            if (smoke && frames >= framesLimit)
                running = false;
        }
        renderer.close();
        need(renderer.errors() == 0, "Stellar sandbox Vulkan validation failed");
        std::cout << "PASS stellar sandbox scenario=" << scenario << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
