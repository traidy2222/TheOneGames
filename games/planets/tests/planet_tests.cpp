#include "planet.hpp"
#include "mesh.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
using namespace planets;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main() {
    try {
        Planet p;double r=p.config().radius;double h=p.height({0,1,0});
        check(p.sample(glm::dvec3(0,r+h+10,0))==Material::Air,"Surface must have air above it");
        check(p.sample(glm::dvec3(0,r*.1,0))==Material::SolidIron,"Inner core phase incorrect");
        check(p.sample(glm::dvec3(0,r*.4,0))==Material::MoltenIron,"Outer core phase incorrect");
        check(p.sample(glm::dvec3(0,r*.8,0))==Material::Mantle,"Mantle missing");
        check(p.cellAt({-.01,-2,-2.01})==Cell{-1,-4,-5},"Negative coordinates must floor");
        check(p.config().cellSize==.5,"New planets must use half-metre excavation cells");
        auto hit=p.ray({0,r+h+20,0},{0,-1,0});check(hit.has_value()&&hit->distance>10&&hit->distance<25,"Axis aligned ray missed surface");
        check(p.sample(hit->previous)==Material::Air,"Ray previous cell must be empty");
        auto pos=p.center(hit->cell);auto first=p.sample(hit->cell);auto count=p.dig(pos,3);
        check(count>0&&p.sample(hit->cell)==Material::Air,"Excavation did not change terrain");
        uint64_t held=0;for(auto n:p.inventory())held+=n;check(held==count,"Dig lost material");
        check(p.dig(pos,3)==0,"Repeated digging duplicated inventory");
        check(p.deposit(pos,3,first)>0,"Deposit failed");
        uint64_t after=0;for(auto n:p.inventory())after+=n;check(after<held,"Deposit did not consume inventory");
        Planet fresh;check(fresh.deposit(pos,3,Material::Soil)==0,"Deposit created material from nothing");
        Planet coarse({250000,2,74123});auto coarsePoint=coarse.center(coarse.cellAt({0,r+h-5,0}));
        coarse.dig(coarsePoint,3);auto oldInventory=coarse.inventory();auto oldEdits=coarse.edits();
        coarse.refine(.5);
        for(size_t i=0;i<materialCount;++i)check(coarse.inventory()[i]==oldInventory[i]*64,"Refinement changed material volume");
        for(const auto& [cell,m]:oldEdits)for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x)
            check(coarse.sample(Cell{cell.x*4+x,cell.y*4+y,cell.z*4+z})==m,"Refinement lost a saved edit");
        for(auto direction:{glm::dvec3(0,-1,0),glm::dvec3(1,0,0),glm::normalize(glm::dvec3(-1,2,3))}) {
            auto surface=direction*(r+p.height(direction)+15);check(p.ray(surface,-direction).has_value(),"Global sampling or ray failed away from north pole");
        }
        auto temp=std::filesystem::temp_directory_path()/("planet-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        struct Clean{std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{temp};
        auto path=temp/"planet.json";p.save(path);Planet loaded;loaded.load(path);
        check(loaded.edits()==p.edits()&&loaded.inventory()==p.inventory(),"Save roundtrip changed world or inventory");
        p.dig(pos,5);p.save(path);loaded.load(path);check(loaded.edits()==p.edits(),"Replacing an existing save failed");
        nlohmann::json valid;{std::ifstream in(path);in>>valid;}
        for(int variant=0;variant<5;++variant) {
            auto invalid=valid;
            if(variant==0)invalid["version"]=99;
            if(variant==1)invalid["inventory"][1]=-1;
            if(variant==2)invalid["edits"][0][3]=999;
            if(variant==3)invalid["radius"]=-1;
            if(variant==4)invalid["edits"].push_back(invalid["edits"][0]);
            {std::ofstream out(path);out<<invalid;}
            auto old=loaded.edits();bool rejected=false;try{loaded.load(path);}catch(...){rejected=true;}
            check(rejected&&loaded.edits()==old,"Malformed load changed live state");
        }
        auto eye=glm::dvec3(0,r+h+10,0);Frame frame(eye);auto mesh=localMesh(p,eye,frame,24);
        check(!mesh.vertices.empty()&&mesh.vertices.size()%3==0,"Local terrain mesh missing");
        size_t smooth=0;
        for(size_t i=0;i<mesh.terrainVertices;++i){const auto& v=mesh.vertices[i];check(std::isfinite(v.position.x)&&std::abs(glm::length(v.normal)-1)<.001,"Mesh contains invalid geometry");
            if(std::abs(v.normal.x)>.01&&std::abs(v.normal.y)>.01&&std::abs(v.normal.z)>.01)++smooth;
        }
        check(smooth>mesh.terrainVertices/4,"Terrain still has axis-aligned block normals");
        // Re-basing must not change global surfaces or introduce cracks in the net.
        Frame shifted(eye+glm::dvec3(.3,.7,-.4));auto other=localMesh(p,eye,shifted,24);
        check(other.terrainVertices==mesh.terrainVertices,"Origin shift changed topology");
        for(size_t i=0;i<mesh.terrainVertices;++i){auto a=frame.origin+frame.direction(mesh.vertices[i].position),b=shifted.origin+shifted.direction(other.vertices[i].position);check(glm::length(a-b)<std::max(.0001,glm::length(glm::dvec3(mesh.vertices[i].position))*2e-7),"Origin shift moved terrain");}
        // Regress the old 146 m cutoff and coarse caves becoming giant sheets.
        auto wide=localMesh(fresh,eye,frame,16);double farthest=0;size_t distant=0;
        using FarKey=std::array<int64_t,3>;std::set<FarKey> stable;
        auto key=[](glm::dvec3 p){return FarKey{int64_t(std::llround(p.x*20)),int64_t(std::llround(p.y*20)),int64_t(std::llround(p.z*20))};};
        for(size_t i=0;i<wide.terrainVertices;++i){
            auto v=wide.vertices[i].position;double distance=glm::length(glm::dvec3(v));farthest=std::max(farthest,distance);
            auto w=frame.origin+frame.direction(v);
            if(distance>200){++distant;double error=std::abs(glm::length(w)-(r+fresh.height(w)));check(error<.02,"Distant terrain is an underground sheet or floating surface");}
            if(std::abs(v.x)>1000&&std::abs(v.z)>1000)stable.insert(key(w));
        }
        check(farthest>7500&&distant>1000,"Distant terrain coverage is missing");
        Frame walked(eye+frame.right*8.0);auto movedMesh=localMesh(fresh,walked.origin,walked,16);size_t matches=0,total=0;
        for(size_t i=0;i<movedMesh.terrainVertices;++i){auto v=movedMesh.vertices[i].position;if(std::abs(v.x)<=1000||std::abs(v.z)<=1000)continue;++total;matches+=stable.contains(key(walked.origin+walked.direction(v)));}
        check(total>0&&matches>total*.9,"Walking reshapes the distant terrain lattice");
        one::Camera longRange;longRange.nearPlane=.2f;longRange.farPlane=terrainFarPlane;
        auto projected=longRange.projection(16.f/9)*glm::vec4(0,0,-8000,1);
        check(projected.z/projected.w>0&&projected.z/projected.w<1,"Eight kilometre terrain is clipped by the camera");
        auto globe=globeMesh(p,true);check(globe.vertices.size()>1000,"Cutaway globe missing");
        Planet underground;glm::dvec3 chamber(0,r*.8,0);underground.dig(chamber,3);
        auto cavity=localMesh(underground,chamber,Frame(chamber),12);
        using Key=std::array<float,3>;std::map<std::pair<Key,Key>,int> edges;
        for(size_t i=0;i<cavity.terrainVertices;i+=3)for(int k=0;k<3;++k){
            auto a=cavity.vertices[i+k].position,b=cavity.vertices[i+(k+1)%3].position;
            Key ka{a.x,a.y,a.z},kb{b.x,b.y,b.z};if(kb<ka)std::swap(ka,kb);++edges[{ka,kb}];
        }
        check(!edges.empty(),"Excavation cavity has no surface");
        for(auto& [edge,count]:edges)check(count==2,"Closed excavation mesh has a crack or duplicate face");
        check(glm::length(frame.local(eye+frame.right*.001)-glm::vec3(.001,0,0))<.00001f,"Observer relative millimetres lost at planetary coordinates");
        std::cout<<"PASS: global layers, negative coordinates, ray traversal, excavation accounting, deposit, transactional save/load, local and globe meshes\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
