#include "scene_handoff.hpp"
#include "scene_asset_reuse.hpp"
#include <chrono>
#include <imgui.h>
#include <unordered_set>
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace
void SceneHandoff::apply(one::PhysicsScene& ready, one::World& world, std::unique_ptr<one::Physics>& physics,
                         one::Renderer& renderer, const std::function<void()>& history,
                         const std::function<void()>& callbacks) {
    stats = {};
    auto total = Clock::now();
    auto start = total;
    auto reused=reuseSceneAssets(ready,world);
    stats.modelsReused=reused.reused;stats.modelsReplaced=reused.replaced;
    renderer.invalidateAssets(reused.obsolete);
    stats.invalidateMs = elapsed(start);
    start = Clock::now();
    history();
    stats.historyMs = elapsed(start);
    start = Clock::now();
    ready.swapInto(world, physics);
    stats.swapMs = elapsed(start);
    start = Clock::now();
    callbacks();
    stats.callbacksMs = elapsed(start);
    stats.totalMs = elapsed(total);
    first = true;
    tracking = true;
}
void SceneHandoff::prepared(double ms) {
    if (tracking && first)
        stats.firstPrepareMs = ms;
}
void SceneHandoff::rendered(double ms, size_t pendingDraws) {
    if (!tracking)
        return;
    if (first)
        stats.firstRenderMs = ms;
    first = false;
    ++stats.uploadFrames;
    if (!pendingDraws)
        tracking = false;
}
void SceneHandoff::draw(const one::Physics& physics) const {
    if (ImGui::Begin("Scene handoff")) {
        auto cache = physics.collisionCacheStats();
        ImGui::Text("Collision cache %.2f MiB | hits %llu | cooks %llu", cache.bytes / 1048576.0,
                    (unsigned long long)cache.hits, (unsigned long long)cache.cooks);
        ImGui::Text("Apply %.3f ms | invalidate %.3f | history %.3f | swap %.3f | callbacks %.3f",
                    stats.totalMs, stats.invalidateMs, stats.historyMs, stats.swapMs, stats.callbacksMs);
        ImGui::Text("First prepare %.3f ms | first render %.3f ms | upload frames %zu", stats.firstPrepareMs,
                    stats.firstRenderMs, stats.uploadFrames);
    }
    ImGui::End();
}
