#include "frame_boundary.hpp"
#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <cstdlib>
FrameBoundary::FrameBoundary() : reference(std::getenv("THEONE_REFERENCE_INPUT_BOUNDARY") != nullptr) {}
bool FrameBoundary::begin(one::Renderer& renderer, SDL_Window* window) const {
    if (reference) return true;
    SDL_PumpEvents();
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return false;
    bool ready = renderer.waitForFrame();
    // Refresh keyboard/gamepad state and collect motion that arrived during the wait.
    // Queued events are retained for the application's normal dispatch below.
    SDL_PumpEvents();
    return ready;
}
