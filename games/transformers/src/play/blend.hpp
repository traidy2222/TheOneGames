#pragma once
#include "../player.hpp"

namespace transformers::play {
// Blend the displayed robot pose, including interrupted transitions, in local TRS.
// Keeping this outside Player leaves deterministic asset inspection unchanged.
class PoseBlend {
    const one::Model* model_=nullptr;
    std::vector<one::Transform> source_;
    float duration_=0,elapsed_=0;
public:
    void capture(const Player& player,float duration=.1f);
    void apply(Player& player,float dt);
    void reset();
    bool active()const{return model_ && elapsed_<duration_;}
};
}
