#version 450
layout(location=0) out vec2 vUv;
void main(){
  uint vid;
#ifdef VULKAN
  vid=gl_VertexIndex;
#else
  vid=uint(gl_VertexID);
#endif
  vec2 uv=vec2((vid<<1)&2u, vid&2u);
  vUv=uv;
  gl_Position=vec4(uv*2.0-1.0,0.999,1.0);
}
