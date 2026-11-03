#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 vertexColor;
layout(location=3) in vec2 texCoord;
layout(location=0) out vec4 outColor;
layout(set=0,binding=3) uniform sampler2D turbulenceTexture;
layout(push_constant) uniform Frame {mat4 vp;vec4 eyeTime;vec4 baseColor;vec4 params;vec4 lighting;} f;
#include "../src/vortex_field.shared"
const vec3 SUN = vec3(-0.650,0.260,-0.714);
const vec3 HAZE = vec3(0.29,0.325,0.31);
float tm(){return f.eyeTime.w;}
float core(){return f.baseColor.y;}
float life(){return f.baseColor.z;}

// One filtered fetch returns the two adjacent lattice planes of a 3-D field.
// Unlike fract(sin(dot())) noise this is stable, cheap and smoothly interpolated.
float noise3(vec3 p){
    vec3 i=floor(p),u=fract(p);u=u*u*u*(u*(u*6.0-15.0)+10.0);
    vec2 uv=i.xy+vec2(37.0,17.0)*i.z+u.xy;
    vec2 rg=textureLod(turbulenceTexture,(uv+0.5)/256.0,0.0).rg;
    return mix(rg.r,rg.g,u.z);
}
float broadNoise(vec3 p){
    return 0.57*noise3(p)+0.29*noise3(p.yzx*2.07+19.3)+0.14*noise3(p.zxy*4.13+41.7);
}
vec2 rotateXZ(vec2 p,float angle){float c=cos(angle),s=sin(angle);return vec2(c*p.x-s*p.y,s*p.x+c*p.y);}
vec3 materialSpace(vec3 p,float rate){
    vec2 q=p.xz-fieldCenter(p.y,tm());
    // Rigid rotation per scale: its spatial derivatives never grow with elapsed time.
    float angular=f.baseColor.x*life()/max(core(),1.0)*rate;
    q=rotateXZ(q,-tm()*angular);
    return vec3(q.x,p.y-tm()*f.baseColor.x*life()*0.22,q.y);
}
float condensation(vec3 p,bool detailed){
    if(p.y<0.0||p.y>1500.0||life()<0.001)return 0.0;
    vec2 q=p.xz-fieldCenter(p.y,tm());
    float radius=fieldRadius(p.y,core());
    float r=length(q);
    if(r>radius*3.0)return 0.0;
    vec3 large=materialSpace(p,0.42);
    float n=broadNoise(large*0.010);
    float smaller=detailed?noise3(materialSpace(p,0.72)*0.042+8.0):0.5;
    // Pressure cooling / saturation gives the condensation threshold. Humidity can
    // retract the visible funnel while the wind and surface debris remain active.
    vec2 perturbed=q/(0.88+0.24*n+0.025*(smaller-0.5));
    vec2 axis=fieldCenter(p.y,tm());
    float water=fieldCondensate(vec3(axis.x+perturbed.x,p.y,axis.y+perturbed.y),tm(),f.baseColor.x,core(),life(),f.params.x);
    float top=1.0-smoothstep(1120.0,1480.0,p.y);
    // 15 micron droplets: extinction = 3 LWC / (2 rho_water r_effective).
    float outer=1.0-smoothstep(radius*2.2,radius*3.0,r);
    return water*100.0*top*outer*(0.85+0.3*n+0.04*(smaller-0.5));
}
float dustDensity(vec3 p,bool detailed){
    if(p.y<0.0||p.y>330.0)return 0.0;
    float source=smoothstep(18.0,60.0,f.baseColor.x*life());
    if(source<0.001)return 0.0;
    float r=length(p.xz-fieldCenter(p.y,tm()));
    float radius=core()*(2.4-0.0035*p.y);
    if(r>radius*1.35+40.0)return 0.0;
    vec3 flow=materialSpace(p,0.28);
    float large=broadNoise(flow*0.014);
    float fine=detailed?noise3(materialSpace(p,0.6)*0.052):0.5;
    float radial=1.0-smoothstep(radius*0.35,radius*(0.7+0.6*large),r);
    float cap=exp(-p.y/(48.0+55.0*large));
    return radial*cap*smoothstep(0.24,0.72,large+0.13*(fine-0.5))*source*1.9;
}
float cloudDensity(vec3 p,bool detailed){
    if(p.y<760.0||p.y>2800.0)return 0.0;
    vec2 q=p.xz-fieldTrack(tm());float r=length(q);
    q=rotateXZ(q,-tm()*0.008);
    vec3 coord=vec3(q.x,p.y*1.7,q.y)*0.0027+vec3(0.0,-tm()*0.009,0.0);
    float n=broadNoise(coord);
    float wall=exp(-r*r/680000.0);
    float base=1390.0-330.0*wall;
    float bottom=base+(n-0.5)*360.0;
    float body=smoothstep(bottom-40.0,bottom+180.0,p.y);
    float detail=detailed?noise3(coord*3.1+13.0):0.5;
    return body*(0.42+0.58*n+0.08*detail)*(1.0-smoothstep(2400.0,2800.0,p.y));
}
// Components are physical path extinction coefficients (1/metre), not alpha-per-sample.
vec3 extinction(vec3 p,bool detailed){
    return vec3(condensation(p,detailed),dustDensity(p,detailed)*0.013,cloudDensity(p,detailed)*0.0045);
}
float totalExtinction(vec3 p){vec3 d=extinction(p,false);return d.x+d.y+d.z;}
vec3 illumination(vec3 p,vec3 densities,vec3 direction){
    float shadow=totalExtinction(p+SUN*14.0)*25.0+
                 totalExtinction(p+SUN*48.0)*55.0+
                 totalExtinction(p+SUN*125.0)*110.0+
                 totalExtinction(p+SUN*285.0)*210.0;
    float sunTrans=exp(-shadow);
    float forward=pow(max(dot(direction,SUN),0.0),5.0);
    float total=max(0.00001,densities.x+densities.y+densities.z);
    vec3 albedo=(vec3(0.91,0.94,0.96)*(densities.x+densities.z)+vec3(0.57,0.43,0.29)*densities.y)/total;
    vec3 ambient=vec3(0.072,0.085,0.092);
    vec3 direct=vec3(0.60,0.56,0.45)*sunTrans*(0.7+forward*1.4);
    return albedo*(ambient+direct);
}
bool slab(float origin,float direction,float bottom,float top,inout float lo,inout float hi){
    if(abs(direction)<0.000001)return origin>=bottom&&origin<=top;
    float a=(bottom-origin)/direction,b=(top-origin)/direction;
    lo=max(lo,min(a,b));hi=min(hi,max(a,b));return hi>lo;
}
void march(vec3 ro,vec3 rd,float start,float end,bool detailed,inout vec3 sum,inout float trans){
    if(end<=start||trans<0.008)return;
    float spacing=detailed?max(3.0,core()/20.0)*(128.0/f.baseColor.w):35.0;
    int limit=detailed?512:128;
    int count=min(limit,max(1,int(ceil((end-start)/spacing))));
    float ds=(end-start)/float(count);
    for(int i=0;i<512;++i){
        if(i>=count||trans<0.008)break;
        // Midpoints and footprint-limited detail avoid screen-locked grain without TAA.
        float distance=start+(float(i)+0.5)*ds;
        vec3 p=ro+rd*distance;
        vec3 d=extinction(p,detailed&&ds<12.0);float total=d.x+d.y+d.z;
        if(total>0.00002){
            float alpha=1.0-exp(-total*ds);
            vec3 light=illumination(p,d,rd);
            light=mix(light,HAZE,1.0-exp(-distance*0.000025));
            sum+=trans*alpha*light;trans*=1.0-alpha;
        }
    }
}
vec3 volume(vec3 ro,vec3 rd,float end,vec3 background){
    float cloudLo=0.0,cloudHi=end;
    bool clouds=slab(ro.y,rd.y,760.0,2800.0,cloudLo,cloudHi);
    vec2 track=fieldTrack(tm());float bound=core()*6.2+240.0;
    float lo=0.0,hi=end;
    bool vortex=slab(ro.x,rd.x,track.x-bound,track.x+bound,lo,hi)&&
                slab(ro.z,rd.z,track.y-bound,track.y+bound,lo,hi)&&
                slab(ro.y,rd.y,0.0,1500.0,lo,hi);
    vec3 sum=vec3(0);float trans=1.0;
    if(vortex){
        if(clouds)march(ro,rd,cloudLo,min(cloudHi,lo),false,sum,trans);
        march(ro,rd,lo,hi,true,sum,trans);
        if(clouds)march(ro,rd,max(cloudLo,hi),cloudHi,false,sum,trans);
    }else if(clouds)march(ro,rd,cloudLo,cloudHi,false,sum,trans);
    return sum+trans*background;
}
vec3 sky(vec3 rd){
    float h=max(rd.y,0.0);
    vec3 col=mix(vec3(0.45,0.48,0.43),vec3(0.13,0.20,0.25),smoothstep(0.0,0.8,h));
    col+=vec3(0.14,0.11,0.065)*pow(max(dot(rd,SUN),0.0),12.0);
    return col;
}
vec3 terrain(vec3 p,float distance){
    vec2 parcel=floor((p.xz+vec2(900,0))/vec2(420,620));
    float field=noise3(vec3(parcel.x*5.0,0,parcel.y*5.0));
    float n=broadNoise(vec3(p.x,0,p.z)*0.05);
    vec3 col=mix(vec3(0.065,0.083,0.033),vec3(0.15,0.13,0.065),field)*(0.8+0.4*n);
    float road=abs(p.x+440.0),aa=max(0.08,distance*0.0005);
    col=mix(col,vec3(0.24,0.22,0.18),1.0-smoothstep(5.4-aa,7.0+aa,road));
    col=mix(col,vec3(0.07,0.079,0.078),1.0-smoothstep(4.5-aa,4.5+aa,road));
    float dash=smoothstep(0.48,0.52,fract(p.z/18.0));
    float stripe=1.0-smoothstep(0.06,0.06+aa,abs(road-0.13));
    col=mix(col,vec3(0.45,0.38,0.19),stripe*dash*min(1.0,0.16/aa));
    col=mix(col,vec3(0.48), (1.0-smoothstep(0.06,0.06+aa,abs(road-4.0)))*min(1.0,0.16/aa));
    vec2 q=p.xz-fieldTrack(tm());col*=1.0-0.25*exp(-dot(q,q)/1400000.0);
    return mix(col,HAZE,1.0-exp(-distance*0.00012));
}
vec3 finish(vec3 color){
    color=vec3(1.0)-exp(-max(color,vec3(0))*1.15);
    if(f.lighting.x<0.5)color=pow(color,vec3(1.0/2.2));
    return color;
}
void main(){
    vec3 ro=f.eyeTime.xyz;
    if(vertexColor.a<1.5){
        vec3 delta=worldPosition-ro;float distance=length(delta);vec3 rd=delta/max(0.001,distance);
        vec3 normal=normalize(worldNormal);if(!gl_FrontFacing)normal=-normal;
        vec3 base=vertexColor.rgb*(0.28+0.55*max(dot(normal,SUN),0.0));
        base=mix(base,HAZE,1.0-exp(-distance*0.00012));
        // Exact same volume integral as the background, stopped at the actual surface.
        outColor=vec4(finish(volume(ro,rd,min(distance,18000.0),base)),1.0);return;
    }
    vec3 rd=normalize(worldPosition);
    float ground=rd.y<-0.000001?-ro.y/rd.y:1e8;
    vec3 background=rd.y<-0.000001?terrain(ro+rd*ground,ground):sky(rd);
    outColor=vec4(finish(volume(ro,rd,min(ground,18000.0),background)),1.0);
}
