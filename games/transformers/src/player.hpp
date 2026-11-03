#pragma once
#include "pose_track.hpp"
#include <theone/render/render_scene.hpp>
#include <optional>
namespace transformers {
class Player {
    const Catalog& catalog_;
    std::vector<int> sequence_;
    int staticPiece_=-1;
    void event(const Json& event, float time);
public:
    std::vector<PoseTrack> tracks;
    std::vector<std::vector<glm::mat4>> poses;
    std::vector<Json> fired;
    glm::mat4 root{1};
    float time=0;
    bool special=false, rootMotion=false;
    explicit Player(const Catalog& catalog);
    void select(int clip);
    void selectPiece(int piece);
    void transform(bool toVehicle, bool running=false);
    void seek(float seconds);
    float duration() const;
    int selected() const { return sequence_.empty() ? -1 : sequence_[0]; }
    const Catalog& catalog() const { return catalog_; }
    std::optional<glm::mat4> placement(size_t piece) const;
    void prepare(one::RenderScene& scene) const;
};
}
