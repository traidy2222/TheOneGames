#include "planet.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace planets {
namespace {
constexpr std::array<Properties,materialCount> materials{{
    {"Air",{0,0,0},0,false}, {"Topsoil",{.19f,.34f,.13f},1500,false},
    {"Sediment",{.56f,.36f,.18f},2200,false}, {"Basalt",{.20f,.24f,.29f},2900,false},
    {"Granite",{.46f,.43f,.41f},2700,false}, {"Iron ore",{.55f,.20f,.10f},4500,false},
    {"Copper ore",{.13f,.56f,.45f},3800,false}, {"Hot mantle",{.55f,.15f,.05f},3300,false},
    {"Molten outer core",{1.0f,.32f,.015f},10000,true}, {"Solid inner core",{1.0f,.76f,.22f},12500,false}
}};
uint64_t mix(uint64_t a) { a^=a>>30;a*=0xbf58476d1ce4e5b9ULL;a^=a>>27;a*=0x94d049bb133111ebULL;return a^(a>>31); }
bool finite(glm::dvec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void validPoint(glm::dvec3 p) { if(!finite(p)||glm::length(p)>1e10)throw std::runtime_error("Position outside supported coordinate range"); }
double noise(glm::dvec3 p,uint32_t seed) {
    auto i=glm::i64vec3(glm::floor(p));auto f=p-glm::dvec3(i);f=f*f*(3.0-2.0*f);
    double result=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        uint64_t h=mix(uint64_t(i.x+x)^mix(uint64_t(i.y+y))^mix(uint64_t(i.z+z)+seed));
        double v=double(h>>11)*(1.0/9007199254740992.0);
        result+=v*(x?f.x:1-f.x)*(y?f.y:1-f.y)*(z?f.z:1-f.z);
    }
    return result;
}
template<class F> void brush(const Planet& world,glm::dvec3 p,double radius,F&& fn) {
    validPoint(p);
    if(!std::isfinite(radius)||radius<=0||radius>12)throw std::runtime_error("Brush radius must be in (0,12] metres");
    auto low=world.cellAt(p-glm::dvec3(radius)),high=world.cellAt(p+glm::dvec3(radius));
    for(auto z=low.z;z<=high.z;++z)for(auto y=low.y;y<=high.y;++y)for(auto x=low.x;x<=high.x;++x) {
        Cell key{x,y,z};auto d=world.center(key)-p;if(glm::dot(d,d)<=radius*radius)fn(key);
    }
}
}
const Properties& properties(Material m) { return materials.at(static_cast<size_t>(m)); }
size_t CellHash::operator()(const Cell& p) const { return size_t(mix(uint64_t(p.x))^mix(uint64_t(p.y)+0x31415)^mix(uint64_t(p.z)+0x73517)); }
Planet::Planet(Config c):config_(c) {
    if(!std::isfinite(c.radius)||c.radius<1000||c.radius>1e7||!std::isfinite(c.cellSize)||c.cellSize<.5||c.cellSize>8)throw std::runtime_error("Invalid planet configuration");
}
Cell Planet::cellAt(glm::dvec3 p) const { validPoint(p);auto v=glm::floor(p/config_.cellSize);return {int64_t(v.x),int64_t(v.y),int64_t(v.z)}; }
glm::dvec3 Planet::center(Cell c) const { return (glm::dvec3(c.x,c.y,c.z)+.5)*config_.cellSize; }
double Planet::height(glm::dvec3 direction) const {
    auto p=glm::normalize(direction)*config_.radius;
    return (noise(p/9000.0,config_.seed)-.5)*1600+(noise(p/650.0,config_.seed+1)-.5)*110+(noise(p/35.0,config_.seed+2)-.5)*16;
}
double Planet::temperature(glm::dvec3 p) const {
    double depth=std::max(0.0,config_.radius-glm::length(p));
    return 15+5500*std::pow(depth/config_.radius,.65);
}
Field Planet::originalField(Cell c) const {
    auto p=center(c);double r=glm::length(p),ratio=r/config_.radius;
    if(ratio<.2)return {config_.cellSize,Material::SolidIron};
    if(ratio<.55)return {config_.cellSize,Material::MoltenIron};
    if(ratio<.96)return {config_.cellSize,Material::Mantle};
    double depth=config_.radius+height(p)-r;
    if(depth<0)return {depth,Material::Air};
    if(depth<3)return {depth,Material::Soil};
    if(depth<11)return {depth,Material::Sediment};
    // Coherent veins and cavities are sampled in global 3D, not tied to a loaded patch.
    double density=depth;
    if(depth>18) density=std::min(depth,std::max((.79-noise(p/24.0,config_.seed+6))*24,(.55-noise(p/55.0,config_.seed+9))*55));
    if(density<0)return {density,Material::Air};
    double vein=noise(p/11.0,config_.seed+4);
    if(vein>.74)return {density,Material::CopperOre};
    if(vein<.26)return {density,Material::IronOre};
    return {density,depth<90?Material::Basalt:Material::Granite};
}
Material Planet::original(Cell c) const { return originalField(c).material; }
Field Planet::field(Cell c) const {
    auto found=edits_.find(c);
    if(found!=edits_.end())return {(found->second==Material::Air?-.5:.5)*config_.cellSize,found->second};
    return originalField(c);
}
Material Planet::sample(Cell c) const { auto found=edits_.find(c);return found==edits_.end()?original(c):found->second; }
uint64_t Planet::dig(glm::dvec3 p,double radius) {
    uint64_t count=0;
    brush(*this,p,radius,[&](Cell c){auto m=sample(c);if(m!=Material::Air){++inventory_[size_t(m)];edits_[c]=Material::Air;++count;}});
    return count;
}
uint64_t Planet::deposit(glm::dvec3 p,double radius,Material m) {
    if(m==Material::Air||size_t(m)>=materialCount)throw std::runtime_error("Invalid deposit material");
    uint64_t count=0;
    brush(*this,p,radius,[&](Cell c){if(inventory_[size_t(m)]&&sample(c)==Material::Air){--inventory_[size_t(m)];edits_[c]=m;++count;}});
    return count;
}
std::optional<Hit> Planet::ray(glm::dvec3 o,glm::dvec3 d,double limit) const {
    validPoint(o);if(!finite(d)||glm::length(d)<1e-12||!std::isfinite(limit)||limit<=0||limit>10000)throw std::runtime_error("Invalid ray");
    d=glm::normalize(d);Cell cell=cellAt(o),previous=cell;double t=0;
    glm::dvec3 delta,next;glm::ivec3 step;
    for(int a=0;a<3;++a) {
        step[a]=d[a]>0?1:-1;
        double coordinate=a==0?double(cell.x):a==1?double(cell.y):double(cell.z);
        double boundary=(coordinate+(step[a]>0?1:0))*config_.cellSize;
        delta[a]=std::abs(d[a])<1e-15?std::numeric_limits<double>::infinity():std::abs(config_.cellSize/d[a]);
        next[a]=std::abs(d[a])<1e-15?std::numeric_limits<double>::infinity():std::max(0.0,(boundary-o[a])/d[a]);
    }
    while(t<=limit) {
        auto m=sample(cell);if(m!=Material::Air)return Hit{cell,previous,o+d*t,t,m};
        int axis=next.x<=next.y&&next.x<=next.z?0:next.y<=next.z?1:2;
        previous=cell;t=next[axis];next[axis]+=delta[axis];
        if(axis==0)cell.x+=step.x;else if(axis==1)cell.y+=step.y;else cell.z+=step.z;
    }
    return {};
}
void Planet::save(const std::filesystem::path& path) const {
    using nlohmann::json;
    json j={{"version",1},{"radius",config_.radius},{"cellSize",config_.cellSize},{"seed",config_.seed},{"inventory",inventory_},{"edits",json::array()}};
    for(const auto& [c,m]:edits_)j["edits"].push_back({c.x,c.y,c.z,int(m)});
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary);out<<j.dump();out.close();if(!out)throw std::runtime_error("Cannot write planet save");}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace planet save");
#else
    std::filesystem::rename(temp,path);
#endif
}
void Planet::refine(double cellSize) {
    if(cellSize==config_.cellSize)return;
    if(!std::isfinite(cellSize)||cellSize<.5||cellSize>config_.cellSize)throw std::runtime_error("Unsupported refinement");
    int factor=int(std::round(config_.cellSize/cellSize));
    if(std::abs(factor*cellSize-config_.cellSize)>1e-9)throw std::runtime_error("Refinement must evenly subdivide cells");
    uint64_t multiplier=uint64_t(factor)*factor*factor;
    if(edits_.size()>1000000/multiplier)throw std::runtime_error("Refinement exceeds edit budget; coarse save retained");
    Planet next({config_.radius,cellSize,config_.seed});
    for(size_t i=0;i<materialCount;++i){
        if(inventory_[i]>1000000000000ULL/multiplier)throw std::runtime_error("Refinement exceeds inventory budget");
        next.inventory_[i]=inventory_[i]*multiplier;
    }
    for(const auto& [c,m]:edits_)for(int z=0;z<factor;++z)for(int y=0;y<factor;++y)for(int x=0;x<factor;++x)
        next.edits_.emplace(Cell{c.x*factor+x,c.y*factor+y,c.z*factor+z},m);
    *this=std::move(next);
}
void Planet::load(const std::filesystem::path& path) {
    using nlohmann::json;
    if(std::filesystem::file_size(path)>128*1024*1024)throw std::runtime_error("Save exceeds 128 MiB limit");
    std::ifstream in(path);json j;in>>j;
    if(j.at("version")!=1)throw std::runtime_error("Unsupported planet save version");
    const auto& seed=j.at("seed");
    if(!seed.is_number_unsigned()||seed.get<uint64_t>()>std::numeric_limits<uint32_t>::max())throw std::runtime_error("Invalid seed");
    Planet next({j.at("radius").get<double>(),j.at("cellSize").get<double>(),seed.get<uint32_t>()});
    const auto& items=j.at("inventory");if(!items.is_array()||items.size()!=materialCount)throw std::runtime_error("Invalid inventory");
    for(size_t i=0;i<materialCount;++i){if(!items[i].is_number_unsigned()||items[i].get<uint64_t>()>1000000000000ULL)throw std::runtime_error("Invalid inventory quantity");next.inventory_[i]=items[i].get<uint64_t>();}
    if(next.inventory_[0]!=0)throw std::runtime_error("Air cannot be held in inventory");
    const auto& edits=j.at("edits");if(!edits.is_array()||edits.size()>1000000)throw std::runtime_error("Invalid edit count");
    for(const auto& e:edits) {
        if(!e.is_array()||e.size()!=4)throw std::runtime_error("Invalid edit row");
        for(int i=0;i<4;++i)if(!e[i].is_number_integer())throw std::runtime_error("Edit values must be integers");
        for(int i=0;i<3;++i)if(std::abs(e[i].get<double>())>1e10/next.config_.cellSize)throw std::runtime_error("Invalid cell coordinate");
        if(e[3].get<double>()<0||e[3].get<double>()>=int(materialCount))throw std::runtime_error("Invalid material");
        Cell c{e[0].get<int64_t>(),e[1].get<int64_t>(),e[2].get<int64_t>()};int m=e[3].get<int>();
        validPoint(next.center(c));
        if(!next.edits_.emplace(c,Material(m)).second)throw std::runtime_error("Duplicate terrain edit");
    }
    *this=std::move(next); // Failed loads never partially replace the running world.
}
}
