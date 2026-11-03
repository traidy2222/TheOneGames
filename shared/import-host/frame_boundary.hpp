#pragma once
namespace one { class Renderer; }
struct SDL_Window;
// Wait before collecting the input that will drive this frame's simulation.
class FrameBoundary {
    bool reference;
public:
    FrameBoundary();
    bool begin(one::Renderer& renderer, SDL_Window* window) const;
};
