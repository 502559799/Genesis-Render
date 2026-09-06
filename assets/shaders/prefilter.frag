#version 450
layout(set=0,binding=0) uniform FrameBlock {
  mat4 viewProj; vec4 cameraPos; vec4 dirLightDir; vec4 dirLightColor;
  vec4 params; vec4 pointPos[4]; vec4 pointData[4]; vec4 misc;
} frame;
layout(set=0,binding=10) uniform sampler2D e0;
layout(set=0,binding=11) uniform sampler2D e1;
layout(set=0,binding=12) uniform sampler2D e2;
layout(set=0,binding=13) uniform sampler2D e3;
layout(set=0,binding=14) uniform sampler2D e4;
layout(set=0,binding=15) uniform sampler2D e5;
layout(location=0) in vec3 vDir;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
const uint SAMPLE_COUNT=64u;
vec2 envDirToFaceUV(vec3 v, out int face){
  vec3 ad=abs(v); vec2 uv;
  if(ad.x>=ad.y&&ad.x>=ad.z){ if(v.x>0){face=0;uv=vec2(-v.z,v.y)/ad.x;}else{face=1;uv=vec2(v.z,v.y)/ad.x;} }
  else if(ad.y>=ad.z){ if(v.y>0){face=2;uv=vec2(v.x,-v.z)/ad.y;}else{face=3;uv=vec2(v.x,v.z)/ad.y;} }
  else { if(v.z>0){face=4;uv=vec2(v.x,v.y)/ad.z;}else{face=5;uv=vec2(-v.x,v.y)/ad.z;} }
  return uv*0.5+0.5;
}
vec3 picke(int f,vec2 uv,float lod){
  vec3 c=vec3(0);
  if(f==0)c=textureLod(e0,uv,lod).rgb; if(f==1)c=textureLod(e1,uv,lod).rgb;
  if(f==2)c=textureLod(e2,uv,lod).rgb; if(f==3)c=textureLod(e3,uv,lod).rgb;
  if(f==4)c=textureLod(e4,uv,lod).rgb; if(f==5)c=textureLod(e5,uv,lod).rgb;
  return c;
}
float radicalInverseVanDerCorput(uint bits){
  bits=(bits<<16u)|(bits>>16u);
  bits=((bits&0x55555555u)<<1u)|((bits&0xAAAAAAAAu)>>1u);
  bits=((bits&0x33333333u)<<2u)|((bits&0xCCCCCCCCu)>>2u);
  bits=((bits&0x0F0F0F0Fu)<<4u)|((bits&0xF0F0F0F0u)>>4u);
  bits=(bits<<8u)|(bits>>8u);
  return float(bits)*2.3283064365386963e-10;
}
vec3 importanceSampleGGX(vec2 xi,vec3 N,float a){
  float phi=2.0*PI*xi.x;
  float cosTheta=sqrt((1.0-xi.y)/(1.0+(a*a-1.0)*xi.y));
  float sinTheta=sqrt(1.0-cosTheta*cosTheta);
  vec3 H=vec3(cos(phi)*sinTheta,sin(phi)*sinTheta,cosTheta);
  vec3 up=abs(N.z)<0.999?vec3(0,0,1):vec3(1,0,0);
  vec3 t=normalize(cross(up,N)); vec3 b=cross(N,t);
  return normalize(t*H.x+b*H.y+N*H.z);
}
float distributionGGX(float NdotH,float r){
  float a=r*r,a2=a*a; float d=NdotH*NdotH*(a2-1.0)+1.0;
  return a2/max(PI*d*d,1e-7);
}
void main(){
  vec3 N=normalize(vDir);
  vec3 V=N;
  float roughness=frame.misc.w;
  vec3 prefiltered=picke(0,vec2(0),0.0)*0.0;
  vec3 R=N;
  if(roughness>0.001){
    vec3 up=abs(N.z)<0.999?vec3(0,0,1):vec3(1,0,0);
    vec3 t=normalize(cross(up,N)); vec3 b=cross(N,t);
    vec3 prefilteredColor=picke(2,vec2(0.5),0.0)*0.0;
    float totalWeight=0;
    for(uint i=0u;i<SAMPLE_COUNT;i++){
      vec2 xi=vec2(float((i+0u)/2u)*0.5+0.25,radicalInverseVanDerCorput(i)+0.25);
      xi=fract(xi);
      vec3 H=importanceSampleGGX(xi,N,roughness);
      vec3 L=normalize(2.0*dot(V,H)*H-V);
      float NdotL=max(dot(N,L),0.0);
      if(NdotL>0){
        int f; vec2 uv=envDirToFaceUV(L,f);
        vec3 sampleCol=picke(f,uv,0.0);
        prefilteredColor+=sampleCol*NdotL; totalWeight+=NdotL;
      }
    }
    prefiltered=prefilteredColor/max(totalWeight,1e-4);
  }
  outColor=vec4(prefiltered,1.0);
}
