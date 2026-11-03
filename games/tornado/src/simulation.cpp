#include "simulation.hpp"
#include <algorithm>
#include <cmath>
namespace tornado {
namespace model {
using glm::vec2; using glm::vec3; using glm::length; using glm::dot;
using glm::max; using glm::clamp; using glm::exp; using glm::sin; using glm::cos; using glm::pow;
#include "vortex_field.shared"
}
glm::vec2 center(float height,float time) { return model::fieldCenter(height,time); }
float radiusAt(float height,const Parameters& p) { return model::fieldRadius(height,p.radius); }
glm::vec3 wind(glm::vec3 position,float time,const Parameters& p) {
    return model::fieldWind(position,time,p.peakWind,p.radius,p.maturity);
}
float liquidWater(glm::vec3 position,float time,const Parameters& p) {
    return model::fieldCondensate(position,time,p.peakWind,p.radius,p.maturity,p.humidity);
}
float Simulation::random() { random_=1664525u*random_+1013904223u;return float(random_>>8)/16777216.f; }
void Simulation::seed(Debris& d,bool airborne) {
    float a=random()*6.2831853f, r=parameters.radius*(.4f+std::sqrt(random())*3.2f);
    auto axis=center(0,time);
    d.position={axis.x+std::cos(a)*r,airborne?.2f+random()*35.f:.18f,axis.y+std::sin(a)*r};
    float kind=random();d.material=kind<.60f?0:kind<.82f?1:kind<.96f?2:3;
    if(d.material==0){d.size=.04f+random()*.16f;d.areaMass=1.5f+random()*3.f;d.lift=.23f;}
    if(d.material==1){d.size=.15f+random()*.32f;d.areaMass=3.f+random()*4.f;d.lift=.30f;}
    if(d.material==2){d.size=.3f+random()*1.4f;d.areaMass=.005f+random()*.015f;d.lift=.12f;}
    if(d.material==3){d.size=.4f+random()*.9f;d.areaMass=.12f+random()*.22f;d.lift=.24f;}
    d.phase=random()*6.2831853f;d.spin=0;
    d.velocity=airborne?wind(d.position,time,parameters)*.45f:glm::vec3(0);
}
Simulation::Simulation(size_t count):debris(count) { reset(); }
void Simulation::reset(float atTime) {
    random_=72849;time=atTime-8.f;
    for(auto& d:debris)seed(d,random()<.12f);
    for(int i=0;i<960;++i)step(1.f/120.f);
    time=atTime;
}
void Simulation::step(float dt) {
    if(!std::isfinite(dt)||dt<=0)return;
    dt=std::min(dt,.1f);
    int steps=std::max(1,int(std::ceil(dt/(1.f/120.f))));float h=dt/steps;
    for(int i=0;i<steps;++i){
        time+=h;
        for(auto& d:debris){
            auto air=wind(d.position,time,parameters), relative=air-d.velocity;
            float relativeSpeed=glm::length(relative);
            float k=.5f*1.225f*1.1f*d.areaMass*relativeSpeed;
            d.velocity+=relative*(1.f-1.f/(1.f+k*h));
            d.velocity.y-=9.81f*h;
            if(d.position.y<.8f){
                float horizontal2=relative.x*relative.x+relative.z*relative.z;
                d.velocity.y+=.5f*1.225f*d.lift*d.areaMass*horizontal2*h;
            }
            d.position+=d.velocity*h;
            if(d.position.y<.15f){
                d.position.y=.15f;d.velocity.y=0;
                float friction=std::exp(-5.f*h);d.velocity.x*=friction;d.velocity.z*=friction;
            }
            float targetSpin=(.1f+std::sin(d.phase)*.045f)*relativeSpeed/(d.size+.2f);
            d.spin+=(targetSpin-d.spin)*(1.f-std::exp(-h*2.f));d.phase+=d.spin*h;
            auto axis=center(0,time);
            if(d.position.y>1600||glm::length(glm::vec2(d.position.x,d.position.z)-axis)>2400)seed(d,false);
        }
    }
}
}
