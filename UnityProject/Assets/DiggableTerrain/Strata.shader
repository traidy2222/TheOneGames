Shader "Excavation/Strata" {
Properties {
_Planet ("Spherical coordinates",Float)=0
_Grass ("Grass albedo",2D)="white" {} _Soil ("Soil albedo",2D)="white" {} _Rock ("Rock albedo",2D)="white" {}
_GrassN ("Grass normal",2D)="bump" {} _SoilN ("Soil normal",2D)="bump" {} _RockN ("Rock normal",2D)="bump" {}
_GrassR ("Grass roughness",2D)="white" {} _SoilR ("Soil roughness",2D)="white" {} _RockR ("Rock roughness",2D)="white" {}
}
SubShader {
Tags { "RenderType"="Opaque" }
CGPROGRAM
#pragma surface surf Standard fullforwardshadows vertex:vert
#pragma target 3.0
float _Planet;sampler2D _Grass,_Soil,_Rock,_GrassN,_SoilN,_RockN,_GrassR,_SoilR,_RockR;
struct Input {float3 worldPos;float3 nWorld;float3 tWorld;float3 bWorld;float4 color:COLOR;};
void vert(inout appdata_full v,out Input o){UNITY_INITIALIZE_OUTPUT(Input,o);float3 n=normalize(v.normal);float3 t=normalize(cross(abs(n.y)>.98?float3(0,0,1):float3(0,1,0),n));v.tangent=float4(t,1);o.nWorld=UnityObjectToWorldNormal(n);o.tWorld=UnityObjectToWorldDir(t);o.bWorld=cross(o.nWorld,o.tWorld);}
float4 tri(sampler2D tex,float3 p,float3 w){return tex2D(tex,p.zy)*w.x+tex2D(tex,p.xz)*w.y+tex2D(tex,p.xy)*w.z;}
float3 tn(sampler2D tex,float3 p,float3 w,float3 n){float3 a=tex2D(tex,p.zy).xyz*2-1,b=tex2D(tex,p.xz).xyz*2-1,c=tex2D(tex,p.xy).xyz*2-1;return normalize(float3(a.z*sign(n.x),a.y,a.x)*w.x+float3(b.x,b.z*sign(n.y),b.y)*w.y+float3(c.x,c.y,c.z*sign(n.z))*w.z);}
void surf(Input IN,inout SurfaceOutputStandard o){
float3 n=normalize(IN.nWorld);float3 w=pow(abs(n),4);w/=max(dot(w,1),.0001);float3 p=IN.worldPos;
float upness=dot(n,lerp(float3(0,1,0),normalize(p),_Planet));float depth=IN.color.r*8;
float grass=IN.color.g*smoothstep(.6,.88,upness);
float rock=smoothstep(1.5,3,depth);rock=max(rock,(1-smoothstep(.35,.7,upness))*.65);
grass*=lerp(1,smoothstep(-.6,.4,length(p)-60),_Planet);float3 weights=float3(grass,(1-grass)*(1-rock),(1-grass)*rock);
float3 g=tri(_Grass,p*.4,w).rgb,s=tri(_Soil,p*.65,w).rgb,r=tri(_Rock,p*.45,w).rgb;
g*=float3(.72,1.12,.72);float macro=.9+.08*sin(p.x*.13+sin(p.z*.08))+.06*sin(p.z*.19+p.x*.07);
float band=1-.08*smoothstep(.65,.85,sin(p.y*7+sin(p.x*.3)*.7+sin(p.z*.2)));
float topsoil=lerp(.55,1,smoothstep(.3,1.3,depth));
o.Albedo=(g*weights.x+s*float3(.88,.78,.65)*weights.y*topsoil+r*weights.z*band)*macro;
float3 wn=normalize(tn(_GrassN,p*.4,w,n)*weights.x+tn(_SoilN,p*.65,w,n)*weights.y+tn(_RockN,p*.45,w,n)*weights.z);
wn=normalize(lerp(n,wn,.45));o.Normal=normalize(float3(dot(wn,normalize(IN.tWorld)),dot(wn,normalize(IN.bWorld)),dot(wn,n)));
float rough=tri(_GrassR,p*.4,w).r*weights.x+tri(_SoilR,p*.65,w).r*weights.y+tri(_RockR,p*.45,w).r*weights.z;
o.Smoothness=clamp(1-rough,0,.28);o.Metallic=0;o.Occlusion=lerp(.85,1,saturate(n.y*.5+.5));
}
ENDCG
}
Fallback "Diffuse"
}
