#include "background_loading.hpp"
#include <imgui.h>
namespace {
bool pending(one::LoadState s) {
    return s == one::LoadState::Queued || s == one::LoadState::Loading;
}
} // namespace
void BackgroundLoads::retireHistory(std::vector<one::World>& undo, std::vector<one::World>& redo) {
    struct History {
        std::vector<one::World> undo, redo;
    };
    auto history = std::make_shared<History>();
    history->undo.swap(undo);
    history->redo.swap(redo);
    scene.retire(std::move(history));
}
void BackgroundLoads::cancel() {
    scene.cancel();
    collision.cancel();
    model.cancel();
    image.cancel();
}
bool BackgroundLoads::busy() const {
    return pending(collision.status().state) || pending(scene.status().state) ||
           pending(model.status().state) || pending(image.status().state);
}
void BackgroundLoads::draw() {
    if (!busy())
        return;
    if (ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pending(collision.status().state))
            ImGui::TextUnformatted("Preparing edited collision...");
        if (pending(scene.status().state))
            ImGui::TextUnformatted("Loading scene and collision...");
        if (pending(model.status().state))
            ImGui::TextUnformatted("Loading model...");
        if (pending(image.status().state))
            ImGui::TextUnformatted("Loading image...");
        if (ImGui::Button("Cancel loading"))
            cancel();
    }
    ImGui::End();
}
void BackgroundLoads::poll(const std::function<void(std::shared_ptr<one::PhysicsScene>)>& applyScene,
                           const std::function<void(std::shared_ptr<one::Model>, bool)>& applyModel,
                           const std::function<void(std::shared_ptr<one::PhysicsEdit>)>& applyCollision,
                           const std::function<bool(const std::shared_ptr<one::Model>&)>& liveModel,
                           const std::function<void(std::string)>& error) {
    auto failure = [&](auto& job) {
        auto s = job.status();
        if (s.state == one::LoadState::Failed) {
            error(s.error);
            job.cancel();
        }
    };
    failure(scene);
    failure(collision);
    failure(model);
    failure(image);
    if (auto ready = scene.take())
        applyScene(std::move(ready));
    if (auto ready = model.take())
        applyModel(std::move(ready), importing);
    if (auto ready = collision.take())
        applyCollision(std::move(ready));
    if (auto ready = image.take())
        if (auto owner = imageOwner.lock(); owner && liveModel(owner) && imageIndex < owner->images.size()) {
            auto& current = owner->images[imageIndex];
            ready->name = current.name;
            ready->revision = current.revision + 1;
            ready->preparedRevision=ready->revision;
            current = std::move(*ready);
        }
}

