Shader "Excavation/Atmosphere" {
SubShader {Tags {"Queue"="Transparent" "RenderType"="Transparent"} Cull Front ZWrite Off Blend SrcAlpha One
Pass {
CGPROGRAM
#pragma vertex vert
#pragma fragment frag
#include "UnityCG.cginc"
struct appdata {float4 vertex:POSITION;float3 normal:NORMAL;};struct v2f {float4 pos:SV_POSITION;float3 world:TEXCOORD0;float3 normal:TEXCOORD1;};
v2f vert(appdata v){v2f o;o.pos=UnityObjectToClipPos(v.vertex);o.world=mul(unity_ObjectToWorld,v.vertex).xyz;o.normal=UnityObjectToWorldNormal(v.normal);return o;}
fixed4 frag(v2f i):SV_Target{float rim=pow(1-abs(dot(normalize(i.normal),normalize(_WorldSpaceCameraPos-i.world))),3);float sun=saturate(dot(normalize(i.normal),normalize(float3(-.4,.6,-.5)))*.5+.5);return float4(.15,.38,.65,rim*.28*sun);}
ENDCG
}}
}
