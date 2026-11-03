#pragma once
#include "catalog.hpp"
namespace transformers {
// Per-instance pose state; model/geometry stay immutable and share GPU assets.
struct PoseTrack {
    const Piece* piece = nullptr;
    std::vector<one::Node> base;
    int clip = -1;
    float started = 0, rate = 1;
    std::string then;
    bool hideWhenDone = false, visible = false;
    std::string attachment;
    void reset(const Piece& value);
    void play(const std::string& name, float time, bool retain);
    std::vector<glm::mat4> pose(float time) const;
    void advance(float time);
    float duration() const;
};
}
