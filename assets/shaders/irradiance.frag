#version 450
layout(set=0,binding=10) uniform sampler2D e0;
layout(set=0,binding=11) uniform sampler2D e1;
layout(set=0,binding=12) uniform sampler2D e2;
layout(set=0,binding=13) uniform sampler2D e3;
layout(set=0,binding=14) uniform sampler2D e4;
layout(set=0,binding=15) uniform sampler2D e5;
layout(location=0) in vec3 vDir;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
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
void main(){
  vec3 n=normalize(vDir);
  vec3 acc=vec3(0);
  float sampleDelta=0.075;
  float nrSamples=0;
  for(float phi=0;phi<2*PI;phi+=sampleDelta){
    for(float theta=0;theta<0.5*PI;theta+=sampleDelta){
      vec3 t=vec3(sin(theta)*cos(phi),sin(theta)*sin(phi),cos(theta));
      vec3 up=abs(n.y)<0.999?vec3(0,1,0):vec3(1,0,0);
      vec3 ta=normalize(cross(up,n)); vec3 tb=cross(n,ta);
      vec3 sv=normalize(ta*cos(phi)*sin(theta)+tb*sin(phi)*sin(theta)+n*cos(theta));
      int f; vec2 uv=envDirToFaceUV(sv,f);
      acc+=picke(f,uv,0.0)*cos(theta)*sin(theta);
      nrSamples++;
    }
  }
  acc*=PI/nrSamples;
  outColor=vec4(acc,1.0);
}
