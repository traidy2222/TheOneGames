#define GLM_ENABLE_EXPERIMENTAL
#include "aim.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <functional>
namespace transformers::play {
bool applyAim(Player& player,float pitch) {
    const int robot=player.catalog().kind("robot");
    if(robot<0||!player.tracks[robot].visible||player.selected()<0)return false;
    const auto& piece=player.catalog().pieces[robot];
    const auto& selected=player.catalog().clips()[player.selected()];
    const auto baseName=stringOr(selected,"animation");
    const int additive=piece.animation(baseName+(pitch>0?".aim_up":".aim_down"));
    if(additive<0)return false;
    const float weight=std::clamp(std::abs(pitch)/1.134464f,0.f,1.f);
    if(weight==0)return true;
    auto& pose=player.poses[robot];const auto& nodes=piece.model->nodes;
    if(pose.size()!=nodes.size())return false;
    std::vector<glm::mat4> local(pose.size());
    for(size_t i=0;i<pose.size();++i) {
        const int parent=nodes[i].parent;
        local[i]=parent<0?pose[i]:std::abs(glm::determinant(glm::mat3(pose[parent])))<1e-10f?nodes[i].transform.matrix():glm::inverse(pose[parent])*pose[i];
    }
    // Exported additive keys contain rest*offset, rather than raw offsets.
    // The original's AdditiveAnimation applies the weighted offset AFTER base rotation.
    // These aim clips are constant four-tick rotation poses; only keyed nodes are touched.
    for(const auto& channel:piece.model->animations[additive].channels) {
        if(channel.values.empty()||channel.node<0)continue;
        const size_t i=size_t(channel.node);if(i>=local.size()||nodes[i].useMatrix)continue;
        glm::vec3 translation,scale,skew;glm::vec4 perspective;glm::quat rotation;
        if(!glm::decompose(local[i],scale,rotation,translation,skew,perspective))continue;
        const auto& rest=nodes[i].transform;const auto value=channel.values.front();
        if(channel.path==1) {
            auto offset=glm::normalize(glm::inverse(rest.rotation)*glm::quat(value.w,value.x,value.y,value.z));
            if(offset.w<0)offset=-offset;
            const auto weighted=glm::normalize(glm::quat(1,0,0,0)*(1-weight)+offset*weight);
            rotation=glm::normalize(rotation*weighted);
        }else if(channel.path==0)translation+=(glm::vec3(value)-rest.position)*weight;
        else if(channel.path==2) {
            const auto denominator=glm::max(glm::abs(rest.scale),glm::vec3(.000001f));
            scale*=glm::mix(glm::vec3(1),glm::vec3(value)/denominator,weight);
        }
        local[i]=glm::translate(glm::mat4(1),translation)*glm::mat4_cast(rotation)*glm::scale(glm::mat4(1),scale);
    }
    std::vector<bool> done(pose.size());
    std::function<void(size_t)> resolve=[&](size_t i){if(done[i])return;const int parent=nodes[i].parent;if(parent>=0)resolve(size_t(parent));pose[i]=parent<0?local[i]:pose[parent]*local[i];done[i]=true;};
    for(size_t i=0;i<pose.size();++i)resolve(i);
    return true;
}
}
