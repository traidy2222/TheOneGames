#include "player.hpp"
namespace transformers {
std::optional<glm::mat4> Player::placement(size_t piece) const {
    const auto& attachment=tracks.at(piece).attachment;
    if(attachment.empty())return root;
    // Native attachment names belong to their owning form, not invariably the robot.
    for(const char* kind:{"robot","vehicle"}) {
        const int parent=catalog_.kind(kind);if(parent<0)continue;
        const int joint=catalog_.pieces[parent].node(attachment);if(joint<0)continue;
        if(!tracks[parent].visible||size_t(parent)>=poses.size()||size_t(joint)>=poses[parent].size())continue;
        return root*poses[parent][joint]*glm::inverse(catalog_.pieces[piece].model->nodes[0].transform.matrix());
    }
    return std::nullopt;
}
void Player::prepare(one::RenderScene& scene) const {
    scene.draws.clear(); scene.transforms.clear(); scene.vertexCount=0;
    const int robot=catalog_.kind("robot");
    auto append=[&](const glm::mat4& m,uint32_t joints=0) {
        one::GeometryTransform t; t.position=m; t.info.x=joints;
        t.normal=std::abs(glm::determinant(glm::mat3(m)))>1e-10f ? glm::mat4(glm::transpose(glm::inverse(glm::mat3(m)))) : glm::mat4(1);
        scene.transforms.push_back(t);
    };
    for(size_t p=0;p<tracks.size();++p) {
        const auto& track=tracks[p]; if(!track.visible) continue;
        const auto& part=catalog_.pieces[p]; const auto& model=*part.model;
        const auto& pose=poses[p]; const auto attached=placement(p);if(!attached)continue;
        const auto placement=*attached;
        const auto& visible=special ? part.specialVisible : part.normalVisible;
        for(size_t n=0;n<model.nodes.size();++n) {
            const auto& node=model.nodes[n];
            if(!node.active || node.mesh<0 || !visible[n]) continue;
            if(std::abs(glm::determinant(glm::mat3(pose[n])))<1e-10f) continue;
            const auto* skin=node.skin>=0 ? &model.skins.at(node.skin) : nullptr;
            const uint32_t palette=uint32_t(scene.transforms.size());
            append(placement*pose[n],skin ? uint32_t(skin->joints.size()) : 0);
            if(skin) for(size_t j=0;j<skin->joints.size();++j)
                append(placement*pose.at(skin->joints[j])*skin->inverseBind.at(j));
            for(const auto& primitive:model.meshes.at(node.mesh)) {
                one::Draw draw; draw.count=uint32_t(primitive.vertices.size()); draw.entity=p+1;
                draw.geometry=&primitive; draw.owner=part.model; draw.palette=palette;
                if(primitive.material>=0) {
                    draw.material=&model.materials.at(primitive.material);
                    if(draw.material->image>=0) draw.image=&model.images.at(draw.material->image);
                }
                scene.vertexCount+=draw.count; scene.draws.push_back(std::move(draw));
            }
        }
    }
}
}
