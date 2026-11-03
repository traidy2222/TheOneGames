#pragma once
#include <theone/physics/scene_loading.hpp>
#include <theone/render/renderer.hpp>
#include <functional>
struct HandoffStats {
    double invalidateMs = 0, historyMs = 0, swapMs = 0, callbacksMs = 0, totalMs = 0;
    double firstPrepareMs = 0, firstRenderMs = 0;
    size_t uploadFrames = 0, modelsReused = 0, modelsReplaced = 0;
};
class SceneHandoff {
    bool first = true, tracking = false;

  public:
    HandoffStats stats;
    void apply(one::PhysicsScene& ready, one::World& world, std::unique_ptr<one::Physics>& physics,
               one::Renderer& renderer, const std::function<void()>& history,
               const std::function<void()>& callbacks);
    void prepared(double ms);
    void rendered(double ms, size_t pendingDraws);
    void draw(const one::Physics& physics) const;
};
