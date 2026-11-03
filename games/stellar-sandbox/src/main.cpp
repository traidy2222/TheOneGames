#include <theone/render/renderer.hpp>
#include "corona.hpp"
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
    c.roll = 0;
    c.verticalFov = 1.05f;
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
        bool validate = false, smoke = false, clean = false, evolve = false;
        int scenario = -1, framesLimit = 12, requestedSteps = 64;
        double requestedTime = -1;
        std::filesystem::path output = "build/captures";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--validate")
                validate = true;
            else if (a == "--smoke-test")
                smoke = true;
            else if (a == "--evolve")
                evolve = true;
            else if (a == "--clean")
                clean = true;
            else if (a == "--scenario" && i + 1 < argc)
                scenario = std::stoi(argv[++i]);
            else if (a == "--frames" && i + 1 < argc)
                framesLimit = std::stoi(argv[++i]);
            else if (a == "--ray-steps" && i + 1 < argc)
                requestedSteps = std::stoi(argv[++i]);
            else if (a == "--time" && i + 1 < argc)
                requestedTime = std::stod(argv[++i]);
            else if (a == "--output" && i + 1 < argc)
                output = argv[++i];
            else
                throw std::runtime_error("Unknown stellar sandbox argument: " + a);
        }
        if (scenario < 0)
            scenario = smoke ? 0 : 11;
        need(scenario >= 0 && scenario <= 13 && framesLimit > 0 && std::isfinite(requestedTime) &&
                 requestedTime >= -1 && requestedTime <= 1e9 && requestedSteps >= 16 && requestedSteps <= 192,
             "Invalid stellar sandbox scenario/frames");
        bool mhd = scenario >= 11;
        std::unique_ptr<CoronaModel> corona;
        if (mhd) {
            need(requestedTime <= 600, "MHD capture pre-roll is limited to 600 seconds");
            corona = std::make_unique<CoronaModel>();
            if (scenario != 12)
                corona->stressField();
            double preroll = requestedTime >= 0 ? requestedTime : scenario == 13 ? 180 : smoke ? 0 : 30;
            if (preroll > 0)
                corona->advance(preroll, 100000);
        }
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
        bool automaticExposure = scenario < 6, eclipse = false, litSide = false;
        bool activity = scenario >= 6, paused = false;
        int view = mhd ? 3 : (scenario == 6 || scenario == 10) ? 1 : scenario == 8 ? 2 : 0;
        int inspection = mhd ? 3 : scenario >= 6 ? (scenario == 7 ? 2 : 1) : 0;
        float timeScale = mhd ? 6.f : 120.f, magneticStrength = .8f, granuleContrast = .05f;
        int raySteps = requestedSteps;
        double simulationTime = corona               ? corona->timeSeconds()
                                : requestedTime >= 0 ? requestedTime
                                : scenario == 10     ? 900
                                                     : 0;
        auto viewExposure = [&] {
            exposureStops = view == 3 && mhd           ? -19.f
                            : (view == 1 || view == 3) ? -16.f
                            : view == 2                ? -4.f
                                                       : -23.f;
            automaticExposure = false;
        };
        if (activity)
            viewExposure();
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
            if (inspection) {
                glm::vec3 centre{float(-double(distanceAU) * astronomicalUnitMetres / unit), 0, 0};
                float r = float(solarRadiusMetres * radiusSolar / unit);
                auto region = stellarActivityRegion(stars.stars[0], 0);
                if (inspection == 3) {
                    glm::vec3 n(region.normal), v(glm::cross(region.normal, region.tangent));
                    float l = float(CoronaModel::lengthMetres / unit);
                    auto target = centre + n * (r + l * .9f);
                    aim(camera, target + v * r * 2.f + n * r * .25f, target);
                    camera.verticalFov = .075f;
                    auto right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0, 1, 0))),
                         up = glm::cross(right, camera.forward());
                    camera.roll = std::atan2(glm::dot(n, right), glm::dot(n, up));
                    return;
                }
                glm::vec3 direction =
                    inspection == 2 ? glm::vec3(0, 0, 1)
                                    : glm::normalize(glm::vec3(glm::cross(region.normal, region.tangent) +
                                                               region.normal * .15));
                aim(camera, centre + direction * r * (inspection == 2 ? 1.012f : 3.3f), centre);
                return;
            }
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
        float previousOffset = 100, previousDistanceAU = distanceAU;
        bool previousEclipse = false;
        bool running = true;
        int frames = 0;
        uint64_t previous = SDL_GetTicksNS();
        std::ofstream metrics(output / "frames.csv");
        metrics << "frame,distance_au,flux_w_m2,visibility,exposure,stellar_time_s,frame_ms,gpu_ms,mhd_steps,"
                   "div_b,kinetic,magnetic_work\n";
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
                    camera.yaw -= e.motion.xrel * .003f * (camera.verticalFov / 1.05f);
                    camera.pitch = std::clamp(
                        camera.pitch - e.motion.yrel * .003f * (camera.verticalFov / 1.05f), -1.5f, 1.5f);
                }
            }
            auto now = SDL_GetTicksNS();
            double dt = std::min(.05, double(now - previous) / 1e9);
            previous = now;
            if (!smoke && !ImGui::GetIO().WantCaptureKeyboard) {
                auto keys = SDL_GetKeyboardState(nullptr);
                auto right = glm::normalize(glm::cross(camera.forward(), camera.up()));
                auto move = camera.forward() * float(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) +
                            right * float(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) +
                            glm::vec3(0, 1, 0) * float(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]);
                if (glm::length(move) > .001f)
                    camera.position += glm::normalize(move) * float(dt) *
                                       (keys[SDL_SCANCODE_LSHIFT] ? 400.f : (inspection ? 45.f : 8.f));
            }
            if (!renderer.beginFrame()) {
                SDL_Delay(5);
                continue;
            }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            bool faceStar = false;
            if ((!smoke || evolve) && !paused) {
                if (mhd && corona) {
                    corona->advance(dt * timeScale);
                    simulationTime = corona->timeSeconds();
                } else
                    simulationTime += dt * timeScale;
            }
            if (!clean) {
                ImGui::SetNextWindowPos({std::max(16.f, ImGui::GetIO().DisplaySize.x - 381.f), 16},
                                        ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize({365, 0}, ImGuiCond_FirstUseEver);
                ImGui::Begin("Stellar sandbox");
                ImGui::PushItemWidth(170);
                ImGui::TextUnformatted(mhd ? "2.5D resistive MHD / coronal region"
                                           : "Procedural stellar activity");
                if (ImGui::Checkbox("MHD active region", &mhd) && mhd && !corona) {
                    corona = std::make_unique<CoronaModel>();
                }
                if (mhd && corona) {
                    if (ImGui::Button("Magnetic region")) {
                        inspection = 3;
                        resetCamera();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stress field"))
                        corona->stressField();
                    auto diagnostics = corona->diagnostics();
                    ImGui::Text("CT divergence %.2e", diagnostics.relativeDivergence);
                    ImGui::Text("Magnetic input %.3e J/m", diagnostics.injectedEnergy * 1.6e13);
                    ImGui::Text("Kinetic energy %.3e J/m", diagnostics.kineticEnergy * 1.6e13);
                }
                ImGui::Checkbox("Surface + plasma", &activity);
                const char* modes[] = {"Visible continuum", "EUV diagnostic (false colour)",
                                       "Coronagraph (visible)", "Coronal EUV (monochrome)"};
                ImGui::SetNextItemWidth(235);
                if (ImGui::Combo("Observer", &view, modes, 4))
                    viewExposure();
                if (ImGui::Button("Whole star")) {
                    inspection = 1;
                    resetCamera();
                    viewExposure();
                }
                ImGui::SameLine();
                if (ImGui::Button("Convection close-up")) {
                    inspection = 2;
                    view = 0;
                    resetCamera();
                    viewExposure();
                }
                if (!mhd)
                    ImGui::SliderFloat("Magnetic activity", &magneticStrength, 0, 2);
                ImGui::SliderFloat("Convection contrast", &granuleContrast, 0, .1f, "%.3f");
                ImGui::SliderInt("Plasma ray steps", &raySteps, 16, 128);
                ImGui::SliderFloat("Time acceleration", &timeScale, 1, mhd ? 30.f : 600.f, "%.0fx",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::Checkbox("Pause evolution", &paused);
                ImGui::SameLine();
                if (ImGui::Button("Replay")) {
                    simulationTime = 0;
                    if (corona) {
                        corona->reset();
                        if (scenario != 12)
                            corona->stressField();
                    }
                }
                ImGui::Text("Stellar time %.1f s", simulationTime);
                ImGui::Separator();
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
                    inspection = 0;
                    resetCamera();
                }
                ImGui::SameLine();
                if (ImGui::Button("Lit side")) {
                    litSide = true;
                    inspection = 0;
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
            if (inspection)
                camera.position.x +=
                    float((double(previousDistanceAU) - distanceAU) * astronomicalUnitMetres / unit);
            previousDistanceAU = distanceAU;
            stars.originMetres = {double(distanceAU) * astronomicalUnitMetres, 0, 0};
            stars.stars[0].kelvin = kelvin;
            stars.stars[0].radiusMetres = solarRadiusMetres * radiusSolar;
            auto& plasma = stars.stars[0].activity;
            plasma.enabled = activity;
            plasma.magneticStrength = magneticStrength;
            plasma.granuleContrast = granuleContrast;
            plasma.atmosphereSteps = uint32_t(raySteps);
            stars.plasma = (mhd && corona && activity) ? corona->snapshot(stars.stars[0]) : nullptr;
            stars.view = static_cast<StellarView>(view);
            stars.timeSeconds = simulationTime;
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
            double exclusion = stars.stars[0].radiusMetres * 1.002;
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
                ImGui::Text("Survey body flux %.6g W/m2", flux);
                ImGui::Text("Substellar visibility %.1f %%", visible * 100);
                ImGui::Text("Angular diameter %.6g deg",
                            stellarAngularRadius(stars.stars[0], worldEye) * 360 / pi);
                ImGui::Text("Exposure multiplier %.6g", lighting.exposure);
                ImGui::TextWrapped("Right mouse looks. WASD flies; Q/E moves vertically; Shift boosts; R "
                                   "resets camera. The observer is a survey camera.");
                ImGui::TextWrapped(
                    "Resolved 2.5D resistive MHD in the active region; a procedural star outside it. Stress "
                    "field adds measured magnetic work. Evolution is CFL limited (four steps/frame). EUV "
                    "emission is synthetic. No full-star MHD or calibrated atomic radiation.");
                ImGui::PopItemWidth();
                ImGui::End();
            }
            renderer.setLighting(lighting);
            renderer.setStellarScene(stars);
            ImGui::Render();
            bool capture = smoke && frames == framesLimit - 1;
            if (renderer.renderCached(vertices, draws, revision, camera, 0, capture, false)) {
                metrics << frames << ',' << distanceAU << ',' << flux << ',' << visible << ','
                        << lighting.exposure << ',' << simulationTime << ','
                        << double(SDL_GetTicksNS() - frameStart) / 1e6 << ','
                        << renderer.frameStats().gpuTotalMs;
                auto diagnostics = (mhd && corona) ? corona->diagnostics() : PlasmaDiagnostics{};
                metrics << ',' << diagnostics.steps << ',' << diagnostics.relativeDivergence << ','
                        << diagnostics.kineticEnergy << ',' << diagnostics.injectedEnergy << '\n';
                if (capture && mhd && corona)
                    corona->saveState(output / "mhd-state.csv");
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
