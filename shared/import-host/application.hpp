#pragma once
#include <filesystem>
struct Options { bool smoke=false,validate=false,robots=false,profileUncapped=false,profileOneFrame=false,profileProbe=false; std::filesystem::path scene,music,visibleProfile; int profileFrames=1200; float spawnX=0,spawnY=2,spawnZ=10; };
int runApplication(const Options& options);
