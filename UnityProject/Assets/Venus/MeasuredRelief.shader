Shader "Venus/MeasuredRelief" {
SubShader {Tags {"RenderType"="Opaque"} Pass {
CGPROGRAM
#pragma vertex vert
#pragma fragment frag
#include "UnityCG.cginc"
struct v2f{float4 pos:SV_POSITION;float3 normal:TEXCOORD0;float4 color:COLOR;};v2f vert(appdata_full v){v2f o;o.pos=UnityObjectToClipPos(v.vertex);o.normal=UnityObjectToWorldNormal(v.normal);o.color=v.color;return o;}fixed4 frag(v2f i):SV_Target{float light=saturate(dot(normalize(i.normal),normalize(float3(.6,.25,-.75))));return float4(i.color.rgb*(.1+light*.9),1);}
ENDCG
}}
}
