#include "application.hpp"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        Options options; options.robots=true;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--smoke-test") options.smoke=true;
            else if(arg=="--validate") options.validate=true;
            else if(arg=="--robots") options.robots=true;
            else if(arg=="--profile-uncapped")options.profileUncapped=true;
            else if(arg=="--profile-one-frame")options.profileOneFrame=true;
            else if(arg=="--profile-probe")options.profileProbe=true;
            else if(arg=="--profile-visible" && i+1<argc)options.visibleProfile=argv[++i];
            else if(arg=="--profile-frames" && i+1<argc)options.profileFrames=std::stoi(argv[++i]);
            else if(arg=="--scene" && i+1<argc) options.scene=argv[++i];
            else if(arg=="--music" && i+1<argc) options.music=argv[++i];
            else if(arg=="--spawn" && i+3<argc) {
                options.spawnX=std::stof(argv[++i]);options.spawnY=std::stof(argv[++i]);options.spawnZ=std::stof(argv[++i]);
                if(!std::isfinite(options.spawnX)||!std::isfinite(options.spawnY)||!std::isfinite(options.spawnZ))throw std::runtime_error("Invalid spawn position");
            }
            else throw std::runtime_error("Unknown or incomplete option: "+arg);
        }
        return runApplication(options);
    } catch(const std::exception& e) { std::cerr<<"The One: "<<e.what()<<'\n'; return 1; }
}
