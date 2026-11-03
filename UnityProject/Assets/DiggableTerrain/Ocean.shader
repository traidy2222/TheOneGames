Shader "Excavation/Ocean" {
Properties { _Color ("Deep water",Color)=(.025,.14,.18,1) }
SubShader { Tags {"RenderType"="Opaque"}
CGPROGRAM
#pragma surface surf Standard fullforwardshadows
#pragma target 3.0
fixed4 _Color;struct Input {float3 worldPos;};
void surf(Input IN,inout SurfaceOutputStandard o){float3 p=IN.worldPos;float wave=sin(p.x*1.4+_Time.y*.4)*sin(p.z*1.6+_Time.y*.3)+sin(p.y*2+_Time.y*.3)*.4;o.Albedo=_Color.rgb;o.Smoothness=.55;o.Metallic=0;}
ENDCG
} Fallback "Diffuse"
}
