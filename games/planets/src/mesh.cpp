#include "mesh.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace planets {
Frame::Frame(glm::dvec3 p):origin(p) {
    up=glm::length(p)>1?glm::normalize(p):glm::dvec3(0,1,0);
    auto reference=std::abs(up.z)<.9?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
    right=glm::normalize(glm::cross(up,reference));back=glm::cross(right,up);
}
glm::vec3 Frame::local(glm::dvec3 p) const { auto d=p-origin;return {glm::dot(d,right),glm::dot(d,up),glm::dot(d,back)}; }
glm::dvec3 Frame::direction(glm::vec3 p) const { return right*double(p.x)+up*double(p.y)+back*double(p.z); }
namespace {
void triangle(Mesh& mesh,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 color,glm::vec3 normal={0,0,0}) {
    auto cross=glm::cross(b-a,c-a);if(glm::length(cross)<1e-7f)return;
    if(glm::length(normal)<.1f)normal=glm::normalize(cross);
    for(auto p:{a,b,c})mesh.vertices.push_back({p,normal,glm::vec4(color,1),{0,0}});
}
}
namespace {
struct SurfacePoint { glm::dvec3 world; glm::vec3 normal; };
void terrainTriangle(Mesh& mesh,const Frame& frame,SurfacePoint a,SurfacePoint b,SurfacePoint c) {
    auto normal=glm::cross(b.world-a.world,c.world-a.world);
    if(glm::dot(normal,normal)<1e-14)return;
    if(glm::dot(normal,a.world+b.world+c.world)<0)std::swap(b,c);
    for(auto v:{a,b,c})mesh.vertices.push_back({frame.local(v.world),glm::normalize(v.normal),
        glm::vec4(properties(Material::Soil).color,float(v.world.z)),{float(v.world.x),float(v.world.y)}});
}
SurfacePoint interpolate(const SurfacePoint& a,const SurfacePoint& b,double t) {
    return {glm::mix(a.world,b.world,t),glm::mix(a.normal,b.normal,float(t))};
}
// Partition triangles at the near-volume box rather than leaving a square hole
// in the ground or drawing an unedited surface over an excavation.
void clippedSurface(Mesh& mesh,const Frame& frame,const SurfacePoint& a,const SurfacePoint& b,const SurfacePoint& c,glm::dvec3 lo,glm::dvec3 hi) {
    for(int axis=0;axis<3;++axis)if(std::max({a.world[axis],b.world[axis],c.world[axis]})<lo[axis]||std::min({a.world[axis],b.world[axis],c.world[axis]})>hi[axis]){terrainTriangle(mesh,frame,a,b,c);return;}
    std::vector<SurfacePoint> remaining{a,b,c};
    for(int axis=0;axis<3&&!remaining.empty();++axis)for(int side:{-1,1}) {
        if(remaining.empty())break;
        double bound=side<0?lo[axis]:hi[axis];
        std::vector<SurfacePoint> inside,outside;
        auto previous=remaining.back();double pd=side*(previous.world[axis]-bound);
        for(auto current:remaining){double cd=side*(current.world[axis]-bound);
            if((pd<=0)!=(cd<=0)){auto crossing=interpolate(previous,current,pd/(pd-cd));inside.push_back(crossing);outside.push_back(crossing);}
            (cd<=0?inside:outside).push_back(current);previous=current;pd=cd;
        }
        for(size_t i=1;i+1<outside.size();++i)terrainTriangle(mesh,frame,outside[0],outside[i],outside[i+1]);
        remaining=std::move(inside);
    }
}
void distantSurface(Mesh& mesh,const Planet& planet,glm::dvec3 anchor,const Frame& frame,Cell base,int half) {
    int axis=0;if(std::abs(anchor.y)>std::abs(anchor[axis]))axis=1;if(std::abs(anchor.z)>std::abs(anchor[axis]))axis=2;
    int u=(axis+1)%3,v=(axis+2)%3;double sign=anchor[axis]>=0?1:-1;
    int64_t components[]{base.x,base.y,base.z};double cell=planet.config().cellSize;
    auto grid=[&](int64_t center){
        std::vector<int64_t> negative,positive,result;int64_t lo=center-half,hi=center+half;
        for(int step=2;step<=4096;step*=2){
            if(step*4*cell>std::min(planet.config().radius*.1,terrainReach*1.1))break;
            int64_t middle=int64_t(std::llround(double(center)/step))*step;
            int64_t radius=(int64_t(std::ceil(double(half)/step))+3)*step;
            while(lo>middle-radius){lo=int64_t(std::ceil(double(lo)/step)-1)*step;negative.push_back(lo);}
            while(hi<middle+radius){hi=int64_t(std::floor(double(hi)/step)+1)*step;positive.push_back(hi);}
        }
        result.assign(negative.rbegin(),negative.rend());
        for(auto i=center-half;i<=center+half;++i)result.push_back(i);
        result.insert(result.end(),positive.begin(),positive.end());return result;
    };
    auto gu=grid(components[u]),gv=grid(components[v]);int nu=int(gu.size()),nv=int(gv.size());
    auto index=[&](int x,int y){return size_t(y)*nu+x;};std::vector<SurfacePoint> points(size_t(nu)*nv);
    auto spacing=[&](const auto& g,int i){return double(std::max(i?g[i]-g[i-1]:int64_t(0),i+1<int(g.size())?g[i+1]-g[i]:int64_t(0)))*cell;};
    for(int y=0;y<nv;++y)for(int x=0;x<nu;++x){
        glm::dvec3 p(0);p[u]=(gu[x]+.5)*cell;p[v]=(gv[y]+.5)*cell;
        double radius=planet.config().radius;
        for(int iteration=0;iteration<8;++iteration){p[axis]=sign*std::sqrt(std::max(1.0,radius*radius-p[u]*p[u]-p[v]*p[v]));radius=planet.config().radius+planet.height(p);}
        Frame tangent(p);double filter=std::max(1.0,std::max(spacing(gu,x),spacing(gv,y))*.5);
        double du=(planet.height(p+tangent.right*filter)-planet.height(p-tangent.right*filter))/(2*filter);
        double dv=(planet.height(p+tangent.back*filter)-planet.height(p-tangent.back*filter))/(2*filter);
        auto normal=glm::normalize(tangent.up-tangent.right*du-tangent.back*dv);
        points[index(x,y)]={p,{glm::dot(normal,frame.right),glm::dot(normal,frame.up),glm::dot(normal,frame.back)}};
    }
    auto center=planet.center(base);auto lo=center-glm::dvec3((half-2)*cell),hi=center+glm::dvec3((half-2)*cell);
    for(int y=0;y<nv-1;++y)for(int x=0;x<nu-1;++x){
        auto a=points[index(x,y)],b=points[index(x+1,y)],c=points[index(x+1,y+1)],d=points[index(x,y+1)];
        clippedSurface(mesh,frame,a,b,c,lo,hi);clippedSurface(mesh,frame,a,c,d,lo,hi);
    }
}
}
Mesh localMesh(const Planet& planet,glm::dvec3 anchor,const Frame& frame,int half) {
    // Only the editable volume is voxel meshed. Distant terrain is a height
    // surface, so caves and differently sized 3D cells cannot create long sheets.
    if(half<2||half>96)throw std::runtime_error("Invalid mesh detail radius");
    auto base=planet.cellAt(anchor);
    base={int64_t(std::llround(double(base.x)/16))*16,int64_t(std::llround(double(base.y)/16))*16,int64_t(std::llround(double(base.z)/16))*16};
    int n=half*2+1;auto index=[&](int x,int y,int z){return(size_t(z)*n+y)*n+x;};
    std::vector<Field> field(size_t(n)*n*n);
    for(int z=0;z<n;++z)for(int y=0;y<n;++y)for(int x=0;x<n;++x)field[index(x,y,z)]=planet.field({base.x+x-half,base.y+y-half,base.z+z-half});
    constexpr int corner[8][3]={{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}};
    constexpr int edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    struct Point{glm::vec3 position,normal{0},color;};
    std::vector<Point> points;std::vector<int> ids(field.size(),-1);std::vector<std::array<int,3>> triangles;
    double cell=planet.config().cellSize;
    bool surfaceView=glm::length(anchor)>planet.config().radius+planet.height(anchor)-8;
    for(int z=0;z<n-1;++z)for(int y=0;y<n-1;++y)for(int x=0;x<n-1;++x){
        Field f[8];glm::dvec3 p[8];int inside=0;
        for(int c=0;c<8;++c){f[c]=field[index(x+corner[c][0],y+corner[c][1],z+corner[c][2])];inside+=f[c].density>0;}
        if(inside==0||inside==8)continue;
        for(int c=0;c<8;++c)p[c]=planet.center({base.x+x+corner[c][0]-half,base.y+y+corner[c][1]-half,base.z+z+corner[c][2]-half});
        glm::dvec3 sum(0);glm::vec3 color(0);int crossings=0;
        for(auto& edge:edges){int a=edge[0],b=edge[1];if((f[a].density>0)==(f[b].density>0))continue;
            double da=std::clamp(f[a].density,-cell,cell),db=std::clamp(f[b].density,-cell,cell);
            sum+=glm::mix(p[a],p[b],da/(da-db));color+=properties(f[a].density>0?f[a].material:f[b].material).color;++crossings;
        }
        auto position=sum/double(crossings);color/=float(crossings);
        // A narrow 4 cm raised overlap hides sub-cell disagreement at the join;
        // it is outside tool reach and never covers the excavation volume.
        if(surfaceView){auto delta=glm::abs(position-planet.center(base));double edge=std::max({delta.x,delta.y,delta.z});
            double overlap=std::clamp((edge-(half-4)*cell)/(2*cell),0.0,1.0)*.04;position+=glm::normalize(position)*overlap;}
        ids[index(x,y,z)]=int(points.size());points.push_back({frame.local(position),glm::vec3(0),color});
    }
    auto emit=[&](int a,int b,int c,glm::vec3 outward){
        if(a<0||b<0||c<0)return;auto normal=glm::cross(points[b].position-points[a].position,points[c].position-points[a].position);
        if(glm::dot(normal,normal)<1e-12f)return;if(glm::dot(normal,outward)<0){std::swap(b,c);normal=-normal;}
        triangles.push_back({a,b,c});for(int i:{a,b,c})points[i].normal+=normal;
    };
    for(int z=1;z<n-1;++z)for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x)for(int axis=0;axis<3;++axis){
        bool inside=field[index(x,y,z)].density>0;if(inside==(field[index(x+(axis==0),y+(axis==1),z+(axis==2))].density>0))continue;
        int a,b,c,d;
        if(axis==0){a=ids[index(x,y-1,z-1)];b=ids[index(x,y,z-1)];c=ids[index(x,y,z)];d=ids[index(x,y-1,z)];}
        else if(axis==1){a=ids[index(x-1,y,z-1)];b=ids[index(x-1,y,z)];c=ids[index(x,y,z)];d=ids[index(x,y,z-1)];}
        else{a=ids[index(x-1,y-1,z)];b=ids[index(x,y-1,z)];c=ids[index(x,y,z)];d=ids[index(x-1,y,z)];}
        if(a<0||b<0||c<0||d<0)continue;glm::dvec3 normal(0);normal[axis]=inside?1:-1;
        glm::vec3 outward{glm::dot(normal,frame.right),glm::dot(normal,frame.up),glm::dot(normal,frame.back)};emit(a,b,c,outward);emit(a,c,d,outward);
    }
    Mesh result;result.vertices.reserve(triangles.size()*3);
    for(auto& p:points)p.normal=glm::length(p.normal)>1e-7f?glm::normalize(p.normal):glm::vec3(0,1,0);
    for(auto& tri:triangles)for(int id:tri){auto& p=points[id];auto w=frame.origin+frame.direction(p.position);result.vertices.push_back({p.position,p.normal,glm::vec4(p.color,float(w.z)),{float(w.x),float(w.y)}});}
    if(surfaceView)distantSurface(result,planet,anchor,frame,base,half);
    result.terrainVertices=uint32_t(result.vertices.size());
    if(surfaceView){
        constexpr double pi=3.141592653589793;
        auto skyPoint=[&](int lat,int lon){double a=pi*(double(lat)/16-.5),b=2*pi*lon/32;return glm::vec3(18000*cos(a)*sin(b),18000*sin(a),18000*cos(a)*cos(b));};
        for(int i=0;i<16;++i)for(int j=0;j<32;++j){auto a=skyPoint(i,j),b=skyPoint(i+1,j),c=skyPoint(i+1,j+1),d=skyPoint(i,j+1);triangle(result,a,b,c,{1,1,1});triangle(result,a,c,d,{1,1,1});}
        for(size_t i=result.terrainVertices;i<result.vertices.size();++i){auto& v=result.vertices[i];float t=std::clamp(v.position.y/18000.f,0.f,1.f);v.color=glm::vec4(glm::mix(glm::vec3(.48f,.57f,.64f),glm::vec3(.10f,.28f,.48f),t),1);}
    }
    return result;
}
Mesh globeMesh(const Planet& world,bool cutaway) {
    constexpr double pi=3.141592653589793;Mesh result;constexpr int lat=48,lon=96;constexpr double radius=55;
    auto point=[&](int i,int j) {double a=pi*(double(i)/lat-.5),b=2*pi*j/lon;glm::dvec3 d{cos(a)*sin(b),sin(a),cos(a)*cos(b)};return glm::vec3(d*(radius*(1+world.height(d)/world.config().radius)));};
    for(int i=0;i<lat;++i)for(int j=0;j<lon;++j) {
        if(cutaway&&j<lon/4)continue;
        auto a=point(i,j),b=point(i+1,j),c=point(i+1,j+1),d=point(i,j+1);
        double h=world.height(glm::dvec3(glm::normalize(a+b+c+d)));
        glm::vec3 color=h<0?glm::vec3(.11f,.28f,.24f):h>220?glm::vec3(.50f,.45f,.32f):glm::vec3(.22f,.39f,.17f);
        triangle(result,a,b,c,color);triangle(result,a,c,d,color);
    }
    if(cutaway) {
        const double rings[]={0,.2,.55,.96,1};
        const Material mats[]={Material::SolidIron,Material::MoltenIron,Material::Mantle,Material::Granite};
        for(int plane=0;plane<2;++plane)for(int ring=0;ring<4;++ring)for(int i=0;i<96;++i) {
            auto p=[&](double r,int k){double a=-pi/2+pi*k/96;return plane==0?glm::vec3(r*cos(a),r*sin(a),0):glm::vec3(0,r*sin(a),r*cos(a));};
            auto a=p(rings[ring]*radius,i),b=p(rings[ring+1]*radius,i),c=p(rings[ring+1]*radius,i+1),d=p(rings[ring]*radius,i+1);
            auto color=properties(mats[ring]).color;
            triangle(result,a,b,c,color);triangle(result,a,c,d,color);
        }
    }
    result.terrainVertices=uint32_t(result.vertices.size());
    return result;
}
}
