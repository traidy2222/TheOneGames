#pragma once
#include "player.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <random>
#include <unordered_map>
namespace transformers {
// Audition native samples with clip timing; FEV spatial/parameter mixing remains in the sidecar.
class Audio {
    struct Sample { SDL_AudioSpec spec{};std::vector<Uint8> bytes; };
    struct Voice {
        SDL_AudioStream* stream=nullptr;std::string event;std::shared_ptr<const Sample> sample;
        bool looping=false,stopping=false;float gain=1;Uint64 started=0,stopAt=0;
        float fadeIn=0,fadeOut=0;
    };
    std::vector<std::unique_ptr<Voice>> voices_;
    std::unordered_map<std::string,std::shared_ptr<const Sample>> samples_;
    size_t sampleBytes_=0;
    std::unordered_map<std::string,size_t> selections_;
    std::mt19937 random_{0x4f505449};
    const Catalog* catalog_=nullptr;
    size_t events_=0;
    int clip_=-1;
    float time_=-1;
    size_t startedVoices_=0;
    void play(const Catalog& catalog,const std::string& name);
    std::shared_ptr<const Sample> sample(const std::filesystem::path& path);
    void reap();
    static void SDLCALL feed(void* userdata,SDL_AudioStream* stream,int additional,int total);
public:
    bool enabled=true;
    ~Audio();
    void clear();
    void trigger(const Catalog& catalog,const std::string& event);
    void stop(const std::string& event);
    void sync(const Player& player,bool playing);
    size_t startedVoices()const{return startedVoices_;}
};
}
