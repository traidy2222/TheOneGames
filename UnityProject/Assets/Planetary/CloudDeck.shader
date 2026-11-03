Shader "Planetary/CloudDeck" {
Properties {_Opacity("Opacity",Range(0,1))=1 _SunDir ("Sun",Vector)=(.6,.25,-.75,0)}
SubShader {Tags {"RenderType"="Transparent" "Queue"="Transparent"} Pass {
Blend SrcAlpha OneMinusSrcAlpha
ZWrite Off
CGPROGRAM
#pragma vertex vert
#pragma fragment frag
#include "UnityCG.cginc"
float _Opacity;float4 _SunDir;struct v2f{float4 pos:SV_POSITION;float3 dir:TEXCOORD0;float3 world:TEXCOORD1;};v2f vert(appdata_base v){v2f o;o.pos=UnityObjectToClipPos(v.vertex);o.dir=normalize(v.vertex.xyz);o.world=mul(unity_ObjectToWorld,v.vertex).xyz;return o;}
float hash(float3 p){return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453);}
float noise(float3 p){float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash(i),hash(i+float3(1,0,0)),f.x),lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x),f.y),lerp(lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x),lerp(hash(i+float3(0,1,1)),hash(i+1),f.x),f.y),f.z);}
float fbm(float3 p){return noise(p)*.5+noise(p*2.1)*.25+noise(p*4.3)*.125+noise(p*8.7)*.0625;}
fixed4 frag(v2f i):SV_Target{float3 n=normalize(i.dir);float warp=fbm(n*6);float cloud=fbm(n*float3(8,19,8)+warp*2);float stripes=sin(n.y*34+warp*7)*.5+.5;float variation=cloud*.75+stripes*.25;float3 color=lerp(float3(.69,.61,.44),float3(.93,.88,.72),variation*.6+.35);float sun=dot(n,normalize(_SunDir.xyz));float diffuse=saturate(sun);float3 view=normalize(_WorldSpaceCameraPos-i.world);float rim=pow(1-saturate(dot(n,view)),3)*saturate(sun+.2);return float4(color*(.012+pow(diffuse,.65)*.94)+float3(.23,.18,.1)*rim,_Opacity);}
ENDCG
}}
}


