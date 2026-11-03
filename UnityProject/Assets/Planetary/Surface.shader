Shader "Planetary/Surface" {
Properties {
 _IgnoreDigHole("Voxel mesh",Float)=0
 _Rock("Albedo",2D)="white"{}
 _Rough("Roughness",2D)="white"{}
 _Bump("Rock normal",2D)="bump"{}
}
SubShader {
 Tags {"RenderType"="Opaque"}
 CGINCLUDE
 #include "UnityCG.cginc"
 #include "Lighting.cginc"
 #if !defined(UNITY_PASS_SHADOWCASTER)
 #include "AutoLight.cginc"
 #endif
 sampler2D _Rock,_Rough,_Bump;
 sampler3D _LampBounce;
 float _IgnoreDigHole,_PlanetFog,_VolumeCoverageReady,_BounceReady;
 float4 _PlanetHaze,_PlanetAmbient,_ObserverChunk,_ObserverInChunk,_BounceMin,_BounceSize;
 StructuredBuffer<int4> _VolumeCoverage;
 struct Input {float4 vertex:POSITION;float3 normal:NORMAL;float3 metric:TEXCOORD1;float4 lighting:COLOR;};

 bool VoxelCovered(float3 world){if(_VolumeCoverageReady<.5)return false;int3 key=(int3)floor((world*_ObserverInChunk.w+_ObserverInChunk.xyz)/16)+(int3)_ObserverChunk.xyz;uint slot=((uint)key.x*73856093u ^ (uint)key.y*19349663u ^ (uint)key.z*83492791u)&8191u;[loop]for(int j=0;j<8192;j++){int4 entry=_VolumeCoverage[slot];if(entry.w==0)return false;if(all(entry.xyz==key))return true;slot=(slot+1)&8191u;}return false;}
 void CutSurface(float3 world){if(_IgnoreDigHole<.5&&VoxelCovered(world))discard;}
 #if !defined(UNITY_PASS_SHADOWCASTER)
 struct Varyings {float4 pos:SV_POSITION;float3 world:TEXCOORD0;float3 normal:TEXCOORD1;float3 metric:TEXCOORD2;float4 lighting:TEXCOORD3;LIGHTING_COORDS(4,5)};
 Varyings vert(Input v){Varyings o;o.pos=UnityObjectToClipPos(v.vertex);o.world=mul(unity_ObjectToWorld,v.vertex).xyz;o.normal=UnityObjectToWorldNormal(v.normal);o.metric=v.metric;o.lighting=v.lighting;TRANSFER_VERTEX_TO_FRAGMENT(o);return o;}
 float3 tri(sampler2D tex,float3 p,float3 w){return tex2D(tex,p.zy).rgb*w.x+tex2D(tex,p.xz).rgb*w.y+tex2D(tex,p.xy).rgb*w.z;}
 void Surface(Varyings i,out float3 albedo,out float rough,out float3 n){n=normalize(i.normal);float3 w=pow(abs(n),4);w/=max(dot(w,float3(1,1,1)),.001);float3 p=i.metric*.5;albedo=tri(_Rock,p,w)*float3(.86,.80,.69);rough=clamp(tri(_Rough,p,w).r,.45,1);float3 a=UnpackNormal(tex2D(_Bump,p.zy)),b=UnpackNormal(tex2D(_Bump,p.xz)),c=UnpackNormal(tex2D(_Bump,p.xy));float3 mapped=normalize(float3(a.z*sign(n.x),a.y,a.x)*w.x+float3(b.x,b.z*sign(n.y),b.y)*w.y+float3(c.x,c.y,c.z*sign(n.z))*w.z);n=normalize(lerp(n,mapped,.35));}
 float3 Direct(float3 albedo,float rough,float3 n,float3 l,float3 v){float nl=saturate(dot(n,l)),nv=max(.001,saturate(dot(n,v)));float3 h=normalize(l+v);float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));float a=rough*rough,a2=a*a;float D=a2/max(.001,UNITY_PI*pow(nh*nh*(a2-1)+1,2));float k=pow(rough+1,2)/8;float G=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));float F=.04+.96*pow(1-vh,5);return (albedo/UNITY_PI+D*G*F/max(.001,4*nl*nv))*nl;}
 float3 Bounce(float3 world,float3 normal){float3 uv=(world*_ObserverInChunk.w+normal*.65-_BounceMin.xyz)/_BounceSize.xyz;if(_BounceReady<.5||any(uv<0)||any(uv>1))return 0;return tex3D(_LampBounce,uv).rgb;}
 float4 fragBase(Varyings i):SV_Target {CutSurface(i.world);float3 albedo,n;float rough;Surface(i,albedo,rough,n);UNITY_LIGHT_ATTENUATION(atten,i,i.world);float visibility=_IgnoreDigHole>.5?i.lighting.a:1;float3 indirect=_IgnoreDigHole>.5?i.lighting.rgb:_PlanetAmbient.rgb*.8;float3 color=Direct(albedo,rough,n,normalize(_WorldSpaceLightPos0.xyz),normalize(-i.world))*_LightColor0.rgb*atten*visibility+albedo*(indirect+Bounce(i.world,n));float fog=exp(-pow(length(i.world)*_PlanetFog,2));return float4(lerp(_PlanetHaze.rgb,color,fog),1);}
 float4 fragAdd(Varyings i):SV_Target {CutSurface(i.world);float3 albedo,n;float rough;Surface(i,albedo,rough,n);UNITY_LIGHT_ATTENUATION(atten,i,i.world);float3 light=normalize(UnityWorldSpaceLightDir(i.world));float3 color=Direct(albedo,rough,n,light,normalize(-i.world))*_LightColor0.rgb*atten;return float4(color*exp(-pow(length(i.world)*_PlanetFog,2)),0);}
 #else
 struct ShadowVaryings {V2F_SHADOW_CASTER;float3 world:TEXCOORD1;};
 ShadowVaryings vertShadow(Input v){ShadowVaryings o;o.world=mul(unity_ObjectToWorld,v.vertex).xyz;TRANSFER_SHADOW_CASTER_NORMALOFFSET(o);return o;}
 float4 fragShadow(ShadowVaryings i):SV_Target {CutSurface(i.world);SHADOW_CASTER_FRAGMENT(i)}
 #endif
 ENDCG
 Pass {
  Tags {"LightMode"="ForwardBase"}
  CGPROGRAM
  #pragma target 4.5
  #pragma vertex vert
  #pragma fragment fragBase
  #pragma multi_compile_fwdbase nolightmap nodirlightmap nodynlightmap novertexlight
  ENDCG
 }
 Pass {
  Tags {"LightMode"="ForwardAdd"}
  Blend One One
  ZWrite Off
  CGPROGRAM
  #pragma target 4.5
  #pragma vertex vert
  #pragma fragment fragAdd
  #pragma multi_compile_fwdadd_fullshadows
  ENDCG
 }
 Pass {
  Tags {"LightMode"="ShadowCaster"}
  ZWrite On
  CGPROGRAM
  #pragma target 4.5
  #pragma vertex vertShadow
  #pragma fragment fragShadow
  #pragma multi_compile_shadowcaster
  ENDCG
 }
}
}
