#define GLM_ENABLE_EXPERIMENTAL
#include "pose_track.hpp"
#include <theone/animation/pose.hpp>
#include <glm/gtx/matrix_decompose.hpp>
namespace transformers {
void PoseTrack::reset(const Piece& value) {
    piece=&value; base.clear(); clip=-1; started=0; rate=1;
    then.clear(); attachment.clear(); visible=false; hideWhenDone=false;
}
float PoseTrack::duration() const { return clip<0 ? 0 : piece->model->animations.at(clip).duration/rate; }
void PoseTrack::play(const std::string& name, float time, bool retain) {
    if (retain && clip>=0) {
        auto detached=base;
        for (auto& n : detached) n.parent=-1;
        const auto local=one::samplePose(detached, piece->model->animations, (time-started)*rate, clip, true, -1,0,1,false,false);
        for (size_t i=0; i<base.size(); ++i) if (!base[i].useMatrix) {
            glm::vec3 skew; glm::vec4 perspective;
            glm::decompose(local[i], base[i].transform.scale, base[i].transform.rotation,
                           base[i].transform.position, skew, perspective);
        }
    } else if (!retain || base.empty()) base=piece->model->nodes;
    clip=piece->animation(name); started=time; rate=1; then.clear(); hideWhenDone=false;
}
std::vector<glm::mat4> PoseTrack::pose(float time) const {
    return one::samplePose(base.empty()?piece->model->nodes:base,piece->model->animations,(time-started)*rate,clip,true,-1,0,1,false,false);
}
void PoseTrack::advance(float time) {
    if (clip>=0 && time>=started+duration()) {
        if (!then.empty()) {
            const auto next=then; const float boundary=started+duration();
            play(next,boundary,true);
        } else if (hideWhenDone) visible=false;
    }
}
}
