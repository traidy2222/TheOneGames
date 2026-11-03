#pragma once
#include "planet.hpp"
#include "content.hpp"
namespace planets {
struct Mesh { std::vector<one::Vertex> vertices; uint32_t terrainVertices=0; };
struct Frame {
    glm::dvec3 origin{0},right{1,0,0},up{0,1,0},back{0,0,1};
    explicit Frame(glm::dvec3 p);
    glm::vec3 local(glm::dvec3 p) const;
    glm::dvec3 direction(glm::vec3 p) const;
};
// Finest excavation samples stay local; coarse surface samples reach ~8 km.
constexpr double terrainReach=8000;
constexpr float terrainFarPlane=20000;
Mesh localMesh(const Planet&,glm::dvec3 anchor,const Frame&,int half=40);
Mesh globeMesh(const Planet&,bool cutaway);
}
