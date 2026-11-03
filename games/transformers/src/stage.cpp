#include "stage.hpp"
#include "player.hpp"
namespace transformers {
void Stage::fitBelow(const one::RenderScene& scene,bool vehicleOnly) {
    vertices.resize(24*24*6);
    // Inspection stage placement, not vehicle physics: keep the native clip pose unchanged.
    float bottom=0;
    if(vehicleOnly) {
        bottom=std::numeric_limits<float>::max();
        for(const auto& d:scene.draws) if(d.geometry) {
            const auto& bounds=d.geometry->bounds;
            if(!bounds.valid())continue;
            const auto& matrix=scene.transforms.at(d.palette).position;
            for(int i=0;i<8;++i) {
                const glm::vec3 p((i&1)?bounds.maximum.x:bounds.minimum.x,(i&2)?bounds.maximum.y:bounds.minimum.y,(i&4)?bounds.maximum.z:bounds.minimum.z);
                bottom=std::min(bottom,(matrix*glm::vec4(p,1)).y);
            }
        }
        if(bottom==std::numeric_limits<float>::max())bottom=0;
    }
    for(auto& vertex:vertices)vertex.position.y=bottom-.02f;
}
void Stage::showMotionCarriers(const Player& player) {
    for(size_t p=0;p<player.tracks.size();++p) {
        const auto& track=player.tracks[p];
        if(!track.visible || (track.piece->kind!="camera_track"&&track.piece->kind!="unbound_motion_carriers"))continue;
        const auto& pose=player.poses[p];
        for(size_t n=1;n<pose.size();++n)for(int axis=0;axis<3;++axis) {
            const float size=track.piece->kind=="camera_track"?.6f:.15f;
            const auto m=player.root*pose[n];
            glm::vec3 along(0),side(0);along[axis]=size;side[(axis+1)%3]=size*.15f;
            glm::vec4 color(.1f,.1f,.1f,1);color[axis]=1;
            for(auto local:{-side,side,along}) {
                one::Vertex v;v.position=glm::vec3(m*glm::vec4(local,1));v.color=color;v.normal={0,1,0};vertices.push_back(v);
            }
        }
    }
    draws[0].count=uint32_t(vertices.size());
}
Stage::Stage() {
    material.name="Character inspection floor";material.roughness=.8f;
    for(int x=-12;x<12;++x)for(int z=-12;z<12;++z) {
        const glm::vec4 color=(x+z)%2 ? glm::vec4(.14f,.18f,.23f,1) : glm::vec4(.18f,.22f,.27f,1);
        const float a=x*2.f,b=z*2.f;
        for(const auto p: {glm::vec3(a,-.02f,b),glm::vec3(a,-.02f,b+2),glm::vec3(a+2,-.02f,b+2),
                          glm::vec3(a,-.02f,b),glm::vec3(a+2,-.02f,b+2),glm::vec3(a+2,-.02f,b)}) {
            one::Vertex v;v.position=p;v.normal={0,1,0};v.color=color;vertices.push_back(v);
        }
    }
    one::Draw draw;draw.count=uint32_t(vertices.size());draw.material=&material;draws.push_back(draw);
}
}
