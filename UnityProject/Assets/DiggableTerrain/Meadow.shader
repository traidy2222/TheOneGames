Shader "Excavation/Meadow" {
SubShader { Tags {"RenderType"="Opaque"} Cull Off
CGPROGRAM
#pragma surface surf Standard vertex:vert addshadow fullforwardshadows
#pragma target 3.0
struct Input {float4 color:COLOR;};
void vert(inout appdata_full v){float weight=saturate(v.color.r*2);v.vertex.x+=sin(_Time.y*1.7+v.vertex.z*.5+v.vertex.x*.3)*.045*weight;}
void surf(Input IN,inout SurfaceOutputStandard o){o.Albedo=IN.color.rgb;o.Smoothness=.1;o.Emission=IN.color.rgb*.12;}
ENDCG
}
Fallback "Diffuse"
}
