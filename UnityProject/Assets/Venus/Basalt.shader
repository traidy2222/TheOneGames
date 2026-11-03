Shader "Venus/Basalt" {
Properties { _Rock("Basalt proxy albedo",2D)="white"{} _Normal("Normal",2D)="bump"{} _Rough("Roughness",2D)="white"{} }
SubShader { Tags {"RenderType"="Opaque"}
CGPROGRAM
#pragma surface surf Standard fullforwardshadows vertex:vert
#pragma target 3.0
sampler2D _Rock,_Normal,_Rough;
struct Input {float3 worldPos;float3 nw;float3 tw;float3 bw;};
void vert(inout appdata_full v,out Input o){UNITY_INITIALIZE_OUTPUT(Input,o);float3 n=normalize(v.normal),t=normalize(cross(abs(n.y)>.98?float3(0,0,1):float3(0,1,0),n));v.tangent=float4(t,1);o.nw=UnityObjectToWorldNormal(n);o.tw=UnityObjectToWorldDir(t);o.bw=cross(o.nw,o.tw);}
float4 tri(sampler2D tex,float3 p,float3 w){return tex2D(tex,p.zy)*w.x+tex2D(tex,p.xz)*w.y+tex2D(tex,p.xy)*w.z;}
void surf(Input IN,inout SurfaceOutputStandard o){float3 n=normalize(IN.nw),w=pow(abs(n),4);w/=max(dot(w,1),.0001);float3 p=IN.worldPos*.65;
float3 albedo=tri(_Rock,p,w).rgb;float strata=.86+.14*smoothstep(-.1,.2,sin(IN.worldPos.y*14+sin(IN.worldPos.x*.4)+sin(IN.worldPos.z*.3)));
o.Albedo=albedo*float3(.86,.80,.69)*strata;
float3 a=tex2D(_Normal,p.zy).xyz*2-1,b=tex2D(_Normal,p.xz).xyz*2-1,c=tex2D(_Normal,p.xy).xyz*2-1;float3 wn=normalize(float3(a.z*sign(n.x),a.y,a.x)*w.x+float3(b.x,b.z*sign(n.y),b.y)*w.y+float3(c.x,c.y,c.z*sign(n.z))*w.z);wn=normalize(lerp(n,wn,.65));o.Normal=normalize(float3(dot(wn,IN.tw),dot(wn,IN.bw),dot(wn,n)));o.Smoothness=min(.22,1-tri(_Rough,p,w).r);o.Metallic=0;}
ENDCG
}Fallback "Diffuse"
}
