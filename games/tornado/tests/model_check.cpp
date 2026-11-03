#include "simulation.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    tornado::Parameters p;
    auto axis=tornado::center(100,18);auto core=tornado::radiusAt(100,p);
    auto v=tornado::wind({axis.x+core,100,axis.y},18,p);
    require(std::abs(v.z-p.peakWind)<8,"Core tangential wind including eddies must approach selected m/s above boundary layer");
    auto outer=tornado::wind({axis.x+core*10,100,axis.y},18,p);
    require(glm::length(outer)<glm::length(v),"Outer wind must decay");
    require(tornado::wind({0,0,0},18,p).y==0,"Ground must have no vertical flow");
    // Independent finite-difference check: coupled inflow/ascent must conserve volume.
    float worstDivergence=0;
    for(float height:{8.f,35.f,150.f,700.f})for(float angle:{0.f,.7f,2.f,4.f})for(float radius:{35.f,140.f,380.f}){
        auto c=tornado::center(height,73);glm::vec3 position{c.x+std::cos(angle)*radius,height,c.y+std::sin(angle)*radius};
        float divergence=0;
        for(int axis=0;axis<3;++axis){glm::vec3 d(0);d[axis]=.5f;divergence+=(tornado::wind(position+d,73,p)[axis]-tornado::wind(position-d,73,p)[axis]);}
        worstDivergence=std::max(worstDivergence,std::abs(divergence));
    }
    require(worstDivergence<.002f,"Prescribed vortex must be approximately divergence-free");
    auto translated=tornado::center(0,118)-tornado::center(0,18);
    require(glm::distance(translated,glm::vec2(160,55))<.01f,"Vortex must translate at the prescribed track speed");
    auto dry=p,wet=p;dry.humidity=.45f;wet.humidity=.9f;
    require(glm::distance(tornado::wind({40,50,0},18,dry),tornado::wind({40,50,0},18,wet))<.001f,"Humidity must change visibility independently of the wind");
    require(tornado::liquidWater({0,10,0},18,wet)>tornado::liquidWater({0,10,0},18,dry),"Moist air must condense more readily");
    require(tornado::liquidWater({2000,10,0},18,wet)==0,"Far-field air below saturation must not become funnel cloud");
    p.maturity=0;require(glm::length(tornado::wind({100,40,20},18,p))==0,"Dissipated vortex has no wind");
    tornado::Simulation a(128),b(128);
    for(int i=0;i<1200;++i){a.step(1.f/120.f);b.step(1.f/120.f);}
    for(size_t i=0;i<a.debris.size();++i){auto& d=a.debris[i];
        require(std::isfinite(glm::length(d.position))&&std::isfinite(glm::length(d.velocity)),"Debris must stay finite");
        require(d.position.y>=.149f,"Debris must collide with ground");
        require(glm::distance(d.position,b.debris[i].position)<.0001f,"Fixed-step replay must be deterministic");
    }
    tornado::Simulation coarse(128),fine(128);
    for(int i=0;i<120;++i){coarse.step(1.f/60.f);fine.step(1.f/120.f);fine.step(1.f/120.f);}
    require(glm::distance(coarse.debris[0].position,fine.debris[0].position)<.01f,"60/120 Hz substeps must agree");
    // Isolate mass response: same geometry/airflow, two area-to-mass ratios.
    tornado::Simulation materials(2);materials.time=18;
    for(auto& d:materials.debris){d.position={40,80,0};d.velocity={0,0,0};d.phase=0;d.lift=.2f;}
    materials.debris[0].areaMass=4;materials.debris[1].areaMass=.006f;
    for(int i=0;i<360;++i)materials.step(1.f/120.f);
    std::cout<<"Debris response in the same updraft: light "<<materials.debris[0].position.y<<" m; timber "<<materials.debris[1].position.y<<" m\n";
    require(materials.debris[0].position.y>90,"Light scraps must follow the updraft");
    require(materials.debris[1].position.y<80,"Heavy timber must fall when weight exceeds aerodynamic support");
    tornado::Simulation supply(512);size_t airborne=0;
    for(const auto& d:supply.debris)if(d.position.y>2)++airborne;
    std::cout<<"Warm circulation has "<<airborne<<" / 512 scraps above 2 metres\n";
    require(airborne>20,"Near-surface eddies must sustain some airborne light debris");
    std::cout<<"PASS: vortex profile, zero penetration, divergence "<<worstDivergence<<", translation, dissipation, ground contact, debris mass response, deterministic replay and timestep agreement\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
