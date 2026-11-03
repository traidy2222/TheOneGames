#include "player.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
using namespace transformers;
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("Usage: transformers_check <variant.json>");
        Catalog c(argv[1]); Player player(c); one::RenderScene scene;
        size_t samples=0, animations=0, vertices=0;
        for(const auto& piece:c.pieces) {
            animations+=piece.model->animations.size();
            for(const auto& mesh:piece.model->meshes) for(const auto& primitive:mesh) vertices+=primitive.vertices.size();
            for(const auto& diagnostic:piece.model->diagnostics) std::cout<<piece.file<<": "<<diagnostic<<'\n';
        }
        for(size_t clip=0;clip<c.clips().size();++clip) {
            player.select(int(clip));
            for(float fraction:{0.f,.25f,.5f,.999f,1.f}) {
                player.seek(player.duration()*fraction); player.prepare(scene);
                for(const auto& pose:player.poses) for(const auto& m:pose) for(int i=0;i<4;++i) for(int j=0;j<4;++j)
                    if(!std::isfinite(m[i][j])) throw std::runtime_error("Non-finite pose: "+c.clips()[clip]["name"].get<std::string>());
                for(const auto& draw:scene.draws)
                    if(draw.palette>=scene.transforms.size()) throw std::runtime_error("Palette range error");
                ++samples;
            }
        }
        const int robot=c.kind("robot"),vehicle=c.kind("vehicle");
        if(vehicle>=0) {
        PoseTrack hold;hold.reset(c.pieces[vehicle]);hold.play("VehicleModel.Trans_R2V",0,false);
        const float end=hold.duration(); const auto before=hold.pose(end);
        hold.play("",end,true);const auto after=hold.pose(end);
        for(size_t n=0;n<before.size();++n)for(int a=0;a<4;++a)for(int b=0;b<4;++b)
            if(std::abs(before[n][a][b]-after[n][a][b])>0.001f)throw std::runtime_error("Retained pose changed: "+c.pieces[vehicle].model->nodes[n].name);
        player.transform(true); player.seek(.2f);
        if(!player.tracks[robot].visible || !player.tracks[vehicle].visible) throw std::runtime_error("R2V overlap missing");
        player.seek(player.duration());
        if(player.tracks[robot].visible || !player.tracks[vehicle].visible) throw std::runtime_error("R2V endpoint wrong");
        for(const char* side:{"L","R"}) {
            const auto& piece=c.pieces[vehicle];const int tyre=piece.node(std::string("Tire_Front_")+side),goal=piece.node(std::string("TireGOAL_Front_")+side);
            if(tyre>=0&&goal>=0&&glm::distance(glm::vec3(player.poses[vehicle][tyre][3]),glm::vec3(player.poses[vehicle][goal][3]))>.001f)
                throw std::runtime_error("A/B transform retained legacy tyre displacement");
        }
        player.transform(false); player.seek(.05f);
        if(!player.tracks[robot].visible || !player.tracks[vehicle].visible) throw std::runtime_error("V2R overlap missing");
        player.seek(player.duration());
        if(!player.tracks[robot].visible || player.tracks[vehicle].visible) throw std::runtime_error("V2R endpoint wrong");
        } else {
            player.transform(true);player.seek(player.duration());
            player.transform(false);player.seek(player.duration());
        }
        const auto& p=c.pieces[robot]; size_t hidden=0;
        for(size_t i=0;i<p.normalVisible.size();++i) if(!p.normalVisible[i]&&p.specialVisible[i]) ++hidden;
        if(!hidden) throw std::runtime_error("Alternate body visibility missing");
        Json report={{"variant",c.data["character"]},{"pieces",c.pieces.size()},{"animations",animations},
            {"clip_entries",c.clips().size()},{"pose_samples",samples},{"vertices",vertices},
            {"hidden_special_nodes",hidden},{"transformation_endpoints","passed"}};
        std::cout<<report.dump(2)<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
