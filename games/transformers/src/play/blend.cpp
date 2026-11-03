#define GLM_ENABLE_EXPERIMENTAL
#include "blend.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cmath>

namespace transformers::play {
namespace {
bool finite(const glm::quat& q){return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w);}
one::Transform localTransform(const one::Model& model,const std::vector<glm::mat4>& pose,size_t i) {
    const auto& node=model.nodes[i];
    glm::mat4 local=pose[i];
    if(node.parent>=0) {
        const auto& parent=pose.at(node.parent);
        // Hidden source nodes can have zero scale; an inverse is undefined there.
        if(std::abs(glm::determinant(glm::mat3(parent)))<1e-10f)return node.transform;
        local=glm::inverse(parent)*local;
    }
    one::Transform result=node.transform;
    glm::vec3 skew;glm::vec4 perspective;
    if(!glm::decompose(local,result.scale,result.rotation,result.position,skew,perspective)||!finite(result.rotation)) {
        result=node.transform;result.position=glm::vec3(local[3]);
        result.scale={glm::length(glm::vec3(local[0])),glm::length(glm::vec3(local[1])),glm::length(glm::vec3(local[2]))};
    }
    result.rotation=glm::normalize(result.rotation);
    return result;
}
}
void PoseBlend::reset(){model_=nullptr;source_.clear();duration_=elapsed_=0;}
void PoseBlend::capture(const Player& player,float duration) {
    reset();const int robot=player.catalog().kind("robot");
    if(robot<0 || robot>=int(player.poses.size()) || !player.tracks[robot].visible || duration<=0)return;
    const auto& model=*player.catalog().pieces[robot].model;const auto& pose=player.poses[robot];
    if(pose.size()!=model.nodes.size())return;
    model_=&model;duration_=duration;source_.reserve(pose.size());
    for(size_t i=0;i<pose.size();++i)source_.push_back(localTransform(model,pose,i));
}
void PoseBlend::apply(Player& player,float dt) {
    if(!active())return;
    const int robot=player.catalog().kind("robot");
    if(robot<0 || robot>=int(player.poses.size()) || player.catalog().pieces[robot].model.get()!=model_ || !player.tracks[robot].visible){reset();return;}
    auto& pose=player.poses[robot];if(pose.size()!=source_.size()){reset();return;}
    elapsed_=std::min(duration_,elapsed_+std::max(0.f,dt));
    if(elapsed_>=duration_){reset();return;}
    const float t=elapsed_/duration_,factor=t*t*(3-2*t);
    std::vector<glm::mat4> local(pose.size());
    for(size_t i=0;i<pose.size();++i) {
        const auto& node=model_->nodes[i];
        if(node.useMatrix){local[i]=node.matrix;continue;}
        auto target=localTransform(*model_,pose,i);const auto& source=source_[i];
        target.position=glm::mix(source.position,target.position,factor);
        target.scale=glm::mix(source.scale,target.scale,factor);
        target.rotation=glm::normalize(glm::slerp(source.rotation,target.rotation,factor));
        local[i]=target.matrix();
    }
    std::vector<bool> resolved(pose.size(),false);
    auto resolve=[&](auto&& self,size_t i)->const glm::mat4& {
        if(!resolved[i]) {
            const int parent=model_->nodes[i].parent;
            pose[i]=parent<0?local[i]:self(self,size_t(parent))*local[i];resolved[i]=true;
        }
        return pose[i];
    };
    for(size_t i=0;i<pose.size();++i)resolve(resolve,i);
}
}
