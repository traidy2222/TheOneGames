Shader "Excavation/PlanetSky" {
Properties { _Radius ("Planet radius",Float)=60 }
SubShader {Tags {"Queue"="Background" "RenderType"="Background" "PreviewType"="Skybox"} Cull Off ZWrite Off
Pass {
CGPROGRAM
#pragma vertex vert
#pragma fragment frag
#include "UnityCG.cginc"
float _Radius;
struct v2f {float4 pos:SV_POSITION;float3 dir:TEXCOORD0;};v2f vert(appdata_base v){v2f o;o.pos=UnityObjectToClipPos(v.vertex);o.dir=v.vertex.xyz;return o;}
fixed4 frag(v2f i):SV_Target{float3 d=normalize(i.dir),up=normalize(_WorldSpaceCameraPos);float h=saturate(dot(d,up));float altitude=length(_WorldSpaceCameraPos)-_Radius;float air=1-smoothstep(10,28,altitude);float3 sky=lerp(float3(.55,.66,.71),float3(.16,.36,.62),pow(h,.45));float sun=pow(saturate(dot(d,normalize(_WorldSpaceLightPos0.xyz))),7000);float star=pow(frac(sin(dot(floor(d*700),float3(12.9898,78.233,19.19)))*43758.5453),180)*.45;return float4(lerp(float3(.002,.004,.009)+star,sky,air)+sun*float3(1,.93,.74),1);}
ENDCG
}}
}
