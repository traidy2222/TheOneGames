#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 vertexColor;
layout(location=3) in vec2 texCoord;

layout(location=0) out vec4 outColor;
#include "scene_uniform.glsl"
layout(push_constant) uniform Frame {mat4 vp;vec4 eyeTime;vec4 baseColor;vec4 params;vec4 lighting;} f;
float hash(vec3 p) {
    uvec3 q=uvec3(ivec3(p));
    uint h=q.x*1597334677u ^ q.y*3812015801u ^ q.z*2798796415u;
    h^=h>>16;h*=2246822519u;h^=h>>13;
    return float(h&0x00ffffffu)/16777215.0;
}
float noise(vec3 p) {
    vec3 i=floor(p),w=fract(p);w=w*w*(3-2*w);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),w.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),w.x),w.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),w.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),w.x),w.y),w.z);
}
void main() {
    vec3 base=vertexColor.rgb*f.baseColor.rgb;
    vec3 n=normalize(worldNormal),l=normalize(scene.lightDirection.xyz);
    if(f.lighting.y>.5) {outColor=vec4(pow(max(base,vec3(0)),vec3(1/2.2)),1);return;}
    // Volumetric grain follows vertical quarry faces as well as the ground.
    vec3 p=vec3(texCoord,vertexColor.a);
    // Grass covers upward topsoil, with exposed earth on steep excavation walls.
    if(base.g>base.r*1.4&&base.r>.14&&base.g<.40)
        base=mix(vec3(.30,.20,.11),base,smoothstep(.65,.95,n.y));
    float scale=length(fwidth(p));
    float fine=mix(noise(p*9),.5,clamp(scale*9,0,1));
    float medium=mix(noise(p*1.7),.5,clamp(scale*1.7,0,1));
    float broad=mix(noise(p*.19),.5,clamp(scale*.19,0,1));
    float detail=.68+.25*medium+.18*fine+.10*broad;
    base*=detail;
    float diffuse=max(dot(n,l),0);
    vec3 color=base*(.32+.85*diffuse);
    float fog=1-exp(-length(worldPosition-f.eyeTime.xyz)*.000055);
    color=mix(color,vec3(.48,.57,.64),fog);
    outColor=vec4(pow(max(color,vec3(0)),vec3(1/2.2)),1);
}
