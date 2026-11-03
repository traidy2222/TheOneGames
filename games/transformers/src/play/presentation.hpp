#pragma once
#include "controller.hpp"
#include <theone/render/camera.hpp>
namespace transformers::play {
struct View {
    float yaw=0,pitch=.32f,distance=29;
    one::Camera camera;
    void update(const Controller& player,const Arena& arena);
};
void hud(const Controller& player,const Arena& arena,bool help);
void shotGeometry(const Controller& player,std::vector<one::Vertex>& vertices);
}
