#include "arena.hpp"
#include <algorithm>
#include <array>
namespace transformers::play {
namespace {
std::shared_ptr<one::Model> cube(glm::vec3 color) {
    auto m=std::make_shared<one::Model>();one::Material material;material.color=glm::vec4(color,1);material.roughness=.85f;m->materials.push_back(material);
    one::Primitive mesh;mesh.material=0;
    for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        glm::vec3 normal(0),u(0),v(0);normal[axis]=sign;u[(axis+1)%3]=1;v[(axis+2)%3]=sign;
        for(auto uv:std::array<glm::vec2,6>{{{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}}) {
            one::SkinVertex vertex;vertex.vertex.position=(normal+u*uv.x+v*uv.y)*.5f;vertex.vertex.normal=normal;
            mesh.bounds.include(vertex.vertex.position);mesh.vertices.push_back(vertex);
        }
    }
    m->meshes.push_back({std::move(mesh)});one::Node node;node.mesh=0;m->nodes.push_back(node);return m;
}
}
uint64_t Arena::box(glm::vec3 p,glm::vec3 size,int material,bool solid,bool dynamic,float slope) {
    one::Entity e;e.id=nextId_++;e.name=dynamic?"Physics target":"Proving ground";e.model=materials_.at(material);
    e.animate=false;e.transform.position=p;e.transform.scale=size;e.transform.rotation=glm::angleAxis(slope,glm::vec3(1,0,0));
    world.entities.push_back(e);
    if(solid) {e.model.reset();e.transform.position/=metresPerUnit;e.transform.scale={1,1,1};e.halfExtent=size/(2*metresPerUnit);e.body=dynamic?"dynamic":"static";e.mass=12;collision.entities.push_back(e);}
    if(dynamic)targets.push_back({e.id});return e.id;
}
Arena::Arena() {
    for(auto color:{glm::vec3(.23f,.29f,.31f),{.045f,.06f,.075f},{.7f,.75f,.7f},{.04f,.35f,.39f},{.8f,.34f,.055f},{.14f,.18f,.21f},{.95f,.7f,.08f},{.12f,.5f,.3f},{.38f,.45f,.49f}})materials_.push_back(cube(color));
    box({0,-2,0},{220,4,220},0);
    // Driving ring and contrasting edge/centre markings.
    for(float side:{-65.f,65.f}) {
        box({0,.035f,side},{150,.07f,20},1,false);box({side,.035f,0},{20,.07f,150},1,false);
        for(int i=-6;i<=6;++i){box({i*10.f,.09f,side},{4,.05f,.35f},6,false);box({side,.09f,i*10.f},{.35f,.05f,4},6,false);}
    }
    box({0,.06f,0},{92,.1f,92},8,false);
    // Raised boundary keeps the whole test area recoverable.
    for(float side:{-108.f,108.f}) {box({side,3,0},{4,6,220},5);box({0,3,side},{220,6,4},5);}
    // Warehouse silhouettes with coloured facades and windows outside the driving lane.
    for(int i=-2;i<=2;++i)for(float side:{-94.f,94.f}) {
        const float h=22.f+float((i+3)%3)*8;
        box({i*36.f,h*.5f,side},{24,h,19},5);
        box({i*36.f,h+1,side},{26,2,21},3);
        for(float y=7;y<h-3;y+=6)for(int w=-2;w<=2;++w)
            box({i*36.f+w*4,y,side+(side<0?9.6f:-9.6f)},{2.3f,2,.15f},3,false);
    }
    // Climbable inclined slabs and catwalk. Boxes carry the same rotations into Jolt.
    box({-30,3,8},{14,1,30},3,true,false,-std::atan2(6.f,30.f));
    box({-30,5.8f,31},{14,1.2f,16},3);
    box({-12,3,34},{10,6,10},5);box({4,5,34},{10,10,10},5);box({21,7,34},{11,14,11},5);
    for(float z:{24.f,38.f})box({-30,6.6f,z},{15,.25f,.5f},6,false);
    // Containers form a small collision/slalom section, leaving generous robot clearance.
    box({-32,3,-26},{16,6,8},4);box({-8,3,-10},{12,6,8},3);box({17,3,-29},{15,6,8},4);
    for(float x:{33.f,43.f})for(float z:{-8.f,5.f,18.f})box({x,2.4f,z},{4.8f,4.8f,4.8f},4,true,true);
    // Target backstop and shooting line.
    box({53,7,5},{2,14,46},5);box({24,.14f,5},{.4f,.1f,42},6,false);
    checkpoints={{55,0,-65},{65,0,0},{55,0,65},{-55,0,65},{-65,0,0},{-55,0,-65}};
    for(auto p:checkpoints) {
        box(p+glm::vec3(0,.12f,0),{12,.15f,12},3,false);
        for(float x:{-8.f,8.f}){box(p+glm::vec3(x,4,0),{.7f,8,.7f},6,false);box(p+glm::vec3(x,8.4f,0),{1.5f,.6f,1.5f},7,false);}
    }
    physics.rebuild(collision);resetPlayer();
}
void Arena::resetPlayer(){physics.resetPlayer((spawn+glm::vec3(0,centreHeight+.1f,0))/metresPerUnit);physics.setRespawnPosition((spawn+glm::vec3(0,centreHeight+.1f,0))/metresPerUnit);}
glm::vec3 Arena::position()const{return physics.playerPosition()*metresPerUnit-glm::vec3(0,centreHeight,0);}
void Arena::step(float dt,glm::vec3 velocity,bool jump) {
    physics.step(collision,dt,velocity/metresPerUnit,jump);
    for(const auto& e:collision.entities)if(e.body=="dynamic")if(auto* rendered=world.find(e.id)) {
        rendered->transform.position=e.transform.position*metresPerUnit;rendered->transform.rotation=e.transform.rotation;
    }
}
std::optional<Hit> Arena::ray(glm::vec3 origin,glm::vec3 direction,float range) const {
    std::optional<Hit> result;
    for(const auto& e:collision.entities) {
        auto inv=glm::inverse(e.transform.rotation);auto p=inv*(origin-e.transform.position*metresPerUnit),d=inv*direction;
        auto half=e.halfExtent*metresPerUnit;float lo=0,hi=range;
        for(int axis=0;axis<3&&hi>=lo;++axis) {
            if(std::abs(d[axis])<1e-6f){if(std::abs(p[axis])>half[axis])hi=-1;}
            else{float a=(-half[axis]-p[axis])/d[axis],b=(half[axis]-p[axis])/d[axis];if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);}
        }
        if(hi>=lo && lo<range){range=lo;result=Hit{e.id,origin+direction*lo,lo};}
    }
    return result;
}
bool Arena::hit(uint64_t id,glm::vec3 direction,float strength) {
    for(auto& t:targets)if(t.id==id){++t.hits;physics.impulse(id,direction*strength);if(t.hits>=3)world.find(id)->model=materials_[7];return true;}return false;
}
int Arena::completedTargets()const{return int(std::count_if(targets.begin(),targets.end(),[](auto& t){return t.hits>=3;}));}
void appendScene(one::RenderScene& destination,const one::RenderScene& source) {
    auto offset=uint32_t(destination.transforms.size());destination.transforms.insert(destination.transforms.end(),source.transforms.begin(),source.transforms.end());
    for(auto draw:source.draws){draw.palette+=offset;draw.entity+=100000;destination.draws.push_back(std::move(draw));}destination.vertexCount+=source.vertexCount;
}
}
