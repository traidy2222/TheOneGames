#pragma once
#include <theone/render/draw.hpp>
#include <theone/render/render_scene.hpp>
namespace transformers {
class Player;
struct Stage {
    one::Material material;
    std::vector<one::Vertex> vertices;
    std::vector<one::Draw> draws;
    Stage();
    void fitBelow(const one::RenderScene& scene, bool vehicleOnly);
    void showMotionCarriers(const Player& player);
};
}
