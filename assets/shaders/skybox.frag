#version 450
layout(set=0,binding=0) uniform FrameBlock {
    mat4 viewProj; vec4 cameraPos; vec4 dirLightDir; vec4 dirLightColor;
    vec4 params; vec4 pointPos[4]; vec4 pointData[4]; vec4 misc;
} frame;
layout(set=0,binding=31) uniform sampler2D equirectMap;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
vec3 rotateY(vec3 v,float a){float c=cos(a),s=sin(a);return vec3(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);}
void main(){
  float u=vUv.x;
  float v=vUv.y;
#ifndef VULKAN
  v=1.0-v;
#endif
  float elev=(0.5-v)*PI;
  float azim=u*2.0*PI;
  vec3 d=normalize(vec3(cos(elev)*sin(azim),sin(elev),cos(elev)*cos(azim)));
  d=rotateY(d,frame.misc.w);
  float tu=atan(d.z,d.x)/(2.0*PI)+0.5;
  float tv=asin(clamp(d.y,-1.0,1.0))/PI+0.5;
  outColor=vec4(textureLod(equirectMap,vec2(tu,1.0-tv),0.0).rgb,1.0);
}
