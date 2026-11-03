#pragma once
#include "../player.hpp"
namespace transformers::play {
// Pitch is radians, positive upwards; native aim reaches full weight at 65 degrees.
// Apply after base pose/blending and before weapon mounting.
bool applyAim(Player& player,float pitch);
}
