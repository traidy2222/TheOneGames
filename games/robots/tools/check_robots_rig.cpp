#include "content.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if(argc!=3 && argc!=4) throw std::runtime_error("Expected original glTF, candidate rig glTF and optional animated glTF");
        auto original=one::Model::load(argv[1]);auto rig=one::Model::load(argv[2]);
        if(rig->skins.size()!=1 || rig->skins[0].joints.size()!=29) throw std::runtime_error("Expected a 29-joint skin");
        std::vector<one::Vertex> a,b,c;std::vector<one::Draw> da,db,dc;
        original->bake(glm::mat4(1),0,0,false,1,a,da);
        rig->bake(glm::mat4(1),0,0,false,1,b,db);
        if(a.empty() || a.size()!=b.size())throw std::runtime_error("Rig and original vertex counts differ");
        float maxPositionError=0,maxNormalError=0;
        for(size_t i=0;i<a.size();++i) {
            maxPositionError=std::max(maxPositionError,glm::distance(a[i].position,b[i].position));
            maxNormalError=std::max(maxNormalError,glm::distance(a[i].normal,b[i].normal));
        }
        if(maxPositionError>1e-4f || maxNormalError>1e-4f)throw std::runtime_error("Candidate does not reproduce original rest pose");
        int joint=rig->skins[0].joints[22];
        rig->nodes[joint].transform.rotation=glm::angleAxis(.35f,glm::vec3(1,0,0));
        rig->bake(glm::mat4(1),0,0,false,1,c,dc);
        size_t moved=0,fixed=0;
        for(size_t i=0;i<b.size();++i) {
            auto distance=glm::distance(b[i].position,c[i].position);
            if(distance>1e-4f)++moved;else ++fixed;
            if(!std::isfinite(distance))throw std::runtime_error("Non-finite deformed vertex");
        }
        if(moved==0 || fixed==0)throw std::runtime_error("Joint motion did not selectively deform the mesh");
        std::cout<<"PASS rest-pose vertices="<<a.size()<<" max-position-error="<<maxPositionError
                 <<" max-normal-error="<<maxNormalError<<" moved="<<moved<<" fixed="<<fixed<<'\n';
        if(argc==4) {
            auto motion=one::Model::load(argv[3]);
            if(motion->animations.empty())throw std::runtime_error("No decoded clips in motion file");
            std::vector<one::Vertex> first,second;std::vector<one::Draw> d1,d2;
            motion->bake(glm::mat4(1),0,0,true,1,first,d1);
            motion->bake(glm::mat4(1),.3f,0,true,1,second,d2);
            if(first.size()!=a.size() || second.size()!=a.size())throw std::runtime_error("Animated vertex count mismatch");
            size_t changed=0;float maxDelta=0;
            for(size_t i=0;i<first.size();++i) {
                auto distance=glm::distance(first[i].position,second[i].position);
                if(!std::isfinite(distance) || distance>5)throw std::runtime_error("Animation produced invalid deformation");
                if(distance>1e-4f)++changed;maxDelta=std::max(maxDelta,distance);
            }
            if(!changed)throw std::runtime_error("Decoded clip did not deform the mesh");
            std::cout<<"PASS sampled-clip="<<motion->animations[0].name<<" changed="<<changed<<" max-delta="<<maxDelta<<'\n';
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
