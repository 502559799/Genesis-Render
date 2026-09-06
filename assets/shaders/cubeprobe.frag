#version 450
layout(set=0,binding=10) uniform samplerCube envMap;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
void main(){
  float u=vUv.x, v=1.0-vUv.y;
  float elev=(0.5-v)*PI;
  float azim=u*2.0*PI;
  vec3 d=vec3(cos(elev)*sin(azim),sin(elev),cos(elev)*cos(azim));
  outColor=vec4(textureLod(envMap,d,0.0).rgb,1.0);
}
