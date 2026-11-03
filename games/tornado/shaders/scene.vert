#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(push_constant) uniform Frame {mat4 vp;vec4 eyeTime;vec4 baseColor;vec4 params;vec4 lighting;} f;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 vertexColor;
layout(location=3) out vec2 texCoord;
void main(){
    vertexColor=color;worldNormal=normal;texCoord=uv;
    if(color.a>1.5){
        gl_Position=vec4(position.xy,.999999,1);
        // Avoid subtracting large positions when reconstructing a direction.
        worldPosition=inverse(mat3(f.vp))*vec3(position.xy,1.0);
    }else{worldPosition=position;gl_Position=f.vp*vec4(position,1);}
}
