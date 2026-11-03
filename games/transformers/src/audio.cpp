#include "audio.hpp"
#include <algorithm>
#include <cmath>
namespace transformers {
Audio::~Audio(){clear();}
void Audio::clear(){for(auto& v:voices_)SDL_DestroyAudioStream(v->stream);voices_.clear();events_=0;}
std::shared_ptr<const Audio::Sample> Audio::sample(const std::filesystem::path& path) {
    const auto key=path.string();if(auto found=samples_.find(key);found!=samples_.end())return found->second;
    auto result=std::make_shared<Sample>();Uint8* bytes=nullptr;Uint32 count=0;
    if(!SDL_LoadWAV(key.c_str(),&result->spec,&bytes,&count))return {};
    result->bytes.assign(bytes,bytes+count);SDL_free(bytes);
    // Keep the audition cache bounded; active streams retain their immutable data.
    constexpr size_t budget=32*1024*1024;
    if(count<=budget) {
        while(sampleBytes_+count>budget&&!samples_.empty()) {
            auto victim=samples_.begin();sampleBytes_-=victim->second->bytes.size();samples_.erase(victim);
        }
        sampleBytes_+=count;samples_[key]=result;
    }
    return result;
}
void SDLCALL Audio::feed(void* userdata,SDL_AudioStream* stream,int additional,int) {
    // The sample is immutable and Voice stays allocated until stream destruction.
    const auto& bytes=static_cast<Voice*>(userdata)->sample->bytes;
    while(additional>0&&!bytes.empty()) {
        if(!SDL_PutAudioStreamData(stream,bytes.data(),int(bytes.size())))break;
        additional-=int(bytes.size());
    }
}
void Audio::reap() {
    const auto now=SDL_GetTicks();
    for(auto it=voices_.begin();it!=voices_.end();) {
        auto& v=**it;
        const float in=v.fadeIn>0?std::min(1.f,float(now-v.started)/v.fadeIn):1.f;
        const float out=v.stopping?(v.fadeOut>0?std::max(0.f,1.f-float(now-v.stopAt)/v.fadeOut):0.f):1.f;
        SDL_SetAudioStreamGain(v.stream,v.gain*in*out);
        if((v.stopping&&out<=0)||(!v.looping&&SDL_GetAudioStreamQueued(v.stream)==0&&SDL_GetAudioStreamAvailable(v.stream)==0)) {
            SDL_DestroyAudioStream(v.stream);it=voices_.erase(it);
        }else ++it;
    }
}
void Audio::stop(const std::string& event) {
    for(auto& v:voices_)if(v->event==event&&!v->stopping){v->stopping=true;v->stopAt=SDL_GetTicks();}
}
void Audio::trigger(const Catalog& catalog,const std::string& event){if(enabled){reap();play(catalog,event);}}
void Audio::play(const Catalog& catalog,const std::string& name) {
    const auto& events=catalog.data.at("sounds").at("events");
    if(!events.contains(name))return;
    const auto& event=events[name];
    if(event.value("loops",false)&&std::any_of(voices_.begin(),voices_.end(),[&](const auto& v){return v->event==name&&!v->stopping;}))return;
    auto random=[&](float span){return std::uniform_real_distribution<float>(-span,span)(random_);};
    size_t layerIndex=0;
    for(const auto& layer:event.at("layers")) {
        const auto key=name+":"+std::to_string(layerIndex++);
        if(layer.at("sounds").empty())continue;
        // No gameplay parameter supplied: audition its initial region.
        const Json* selected=nullptr;
        for(const auto& s:layer["sounds"])if(s.value("start",0.f)<=0){selected=&s;break;}
        if(!selected)continue;const auto& sound=*selected;
        const auto& choices=sound.at("samples");if(choices.empty())continue;
        size_t index=0;auto& previous=selections_[key];const auto mode=sound.value("play_mode",std::string("sequential"));
        if(mode=="sequential")index=previous%choices.size();
        else {
            std::vector<double> weights;for(const auto& c:choices)weights.push_back(std::max(0.f,c.value("weight",100.f)));
            if(mode=="random_no_repeat"&&previous&&choices.size()>1)weights[(previous-1)%choices.size()]=0;
            index=std::discrete_distribution<size_t>(weights.begin(),weights.end())(random_);
        }
        previous=mode=="sequential"?previous+1:index+1;
        if(!choices[index].contains("file")||!choices[index]["file"].is_string())continue;
        auto data=sample(catalog.folder/choices[index]["file"].get<std::string>());if(!data||data->bytes.empty())continue;
        auto voice=std::make_unique<Voice>();voice->event=name;voice->sample=std::move(data);
        voice->looping=event.value("loops",false)&&!sound.value("one_shot",true);
        voice->gain=std::clamp(event.value("volume",1.f)*event.value("category_volume",1.f)*sound.value("volume",1.f)*sound.value("definition_volume",1.f)*(1+random(event.value("volume_random",0.f)))*.5f,0.f,1.f);
        voice->fadeIn=event.value("fade_in_ms",0.f);voice->fadeOut=event.value("fade_out_ms",0.f);voice->started=SDL_GetTicks();
        voice->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&voice->sample->spec,voice->looping?feed:nullptr,voice->looping?voice.get():nullptr);
        if(!voice->stream)continue;
        const auto pitch=event.value("pitch_octaves",0.f)+sound.value("definition_pitch_octaves",0.f)+random(event.value("pitch_random_octaves",0.f))+random(sound.value("definition_pitch_random_octaves",0.f));
        SDL_SetAudioStreamFrequencyRatio(voice->stream,std::clamp(std::exp2(pitch),.25f,4.f));
        SDL_SetAudioStreamGain(voice->stream,voice->fadeIn>0?0.f:voice->gain);
        if(!voice->looping){SDL_PutAudioStreamData(voice->stream,voice->sample->bytes.data(),int(voice->sample->bytes.size()));SDL_FlushAudioStream(voice->stream);}
        SDL_ResumeAudioStreamDevice(voice->stream);voices_.push_back(std::move(voice));++startedVoices_;
    }
    while(voices_.size()>24){SDL_DestroyAudioStream(voices_.front()->stream);voices_.erase(voices_.begin());}
}
void Audio::sync(const Player& p,bool playing) {
    reap();
    if(!enabled||!playing){clear();events_=p.fired.size();time_=p.time;return;}
    const bool changed=catalog_!=&p.catalog()||clip_!=p.selected();
    if(changed||p.time<time_) {
        // Keep one-shot tails across transitions; only sustain belongs to the old state.
        if(changed)for(auto& v:voices_)if(v->looping&&!v->stopping){v->stopping=true;v->stopAt=SDL_GetTicks();}
        events_=0;catalog_=&p.catalog();clip_=p.selected();
    }
    if(events_>p.fired.size())events_=0;
    for(;events_<p.fired.size();++events_) {
        const auto& e=p.fired[events_];const std::string type=e["type"];
        if(type=="sound_play")play(p.catalog(),stringOr(e,"event"));
        else if(type=="sound_stop")stop(stringOr(e,"event"));
        else if(type=="footstep") {
            const auto& steps=p.catalog().data["character_sounds"]["by_surface"]["step"];
            if(!steps.empty())play(p.catalog(),stringOr(steps[0],"sound"));
        }
    }
    time_=p.time;
}
}
