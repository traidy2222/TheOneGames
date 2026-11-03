Shader "Venus/SurfaceHaze" {
SubShader {Tags {"Queue"="Background" "RenderType"="Background" "PreviewType"="Skybox"} Cull Off ZWrite Off
Pass {
CGPROGRAM
#pragma vertex vert
#pragma fragment frag
#include "UnityCG.cginc"
struct v2f{float4 pos:SV_POSITION;float3 dir:TEXCOORD0;};v2f vert(appdata_base v){v2f o;o.pos=UnityObjectToClipPos(v.vertex);o.dir=v.vertex.xyz;return o;}
fixed4 frag(v2f i):SV_Target{float y=saturate(normalize(i.dir).y);return float4(lerp(float3(.48,.33,.16),float3(.29,.19,.085),pow(y,.6)),1);}
ENDCG
}}
}
