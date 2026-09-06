#version 450
layout(set=0,binding=0) uniform FrameBlock {
    mat4 viewProj; vec4 cameraPos; vec4 dirLightDir; vec4 dirLightColor;
    vec4 params; vec4 pointPos[4]; vec4 pointData[4]; vec4 misc;
} frame;
layout(set=0,binding=2) uniform MaterialBlock {
    vec4 baseColorFactor;
    vec4 pbr;
    vec4 emissive;
    vec4 texMask;
    vec4 texMask2;
} matl;
layout(set=0,binding=11) uniform sampler2D irrAtlas;
layout(set=0,binding=12) uniform sampler2D equirectMap;
layout(set=0,binding=13) uniform sampler2D brdfLUT;
layout(set=0,binding=20) uniform sampler2D albedoTex;
layout(set=0,binding=21) uniform sampler2D normalTex;
layout(set=0,binding=22) uniform sampler2D mrTex;
layout(set=0,binding=23) uniform sampler2D aoTex;
layout(set=0,binding=24) uniform sampler2D emissiveTex;
layout(set=0,binding=1) uniform DrawBlock {
    mat4 model; mat4 normalMatrix; vec4 tint;
} draw;

layout(location=0) in vec3 vWorldPos;
layout(location=1) in vec3 vNormal;
layout(location=2) in vec4 vTangent;
layout(location=3) in vec2 vUv;
layout(location=0) out vec4 outColor;

const float PI=3.14159265359;
const float MAX_REFLECTION_LOD=8.0;

vec2 envDirToFaceUV(vec3 v, out int face){
  vec3 ad=abs(v); vec2 uv;
  if(ad.x>=ad.y&&ad.x>=ad.z){ if(v.x>0){face=0;uv=vec2(-v.z,v.y)/ad.x;}else{face=1;uv=vec2(v.z,v.y)/ad.x;} }
  else if(ad.y>=ad.z){ if(v.y>0){face=2;uv=vec2(v.x,-v.z)/ad.y;}else{face=3;uv=vec2(v.x,v.z)/ad.y;} }
  else { if(v.z>0){face=4;uv=vec2(v.x,v.y)/ad.z;}else{face=5;uv=vec2(-v.x,v.y)/ad.z;} }
  return uv*0.5+0.5;
}

struct PBR { vec3 albedo; float metallic; float roughness; vec3 normal; float ao; vec3 emissive; };
float D_GGX(float ndh,float r){float a=r*r,a2=a*a;float d=ndh*ndh*(a2-1)+1;return a2/max(PI*d*d,1e-7);}
float SchlickGGX(float x,float k){return x/(x*(1-k)+k);}
float G_Smith(float ndv,float ndl,float r){float k=(r+1);k=k*k/8;return SchlickGGX(ndv,k)*SchlickGGX(ndl,k);}
vec2 atlasUV(int f,vec2 uv){return vec2((float(f)+uv.x)/6.0,uv.y);}
vec2 envUV(vec3 d){return vec2(atan(d.z,d.x)/(2.0*PI)+0.5, 1.0-(asin(clamp(d.y,-1.0,1.0))/PI+0.5));}
vec3 F_Schlick(float c,vec3 f0){return f0+(1-f0)*pow(clamp(1-c,0,1),5);}

vec3 F_SchlickRoughness(float c,vec3 f0,float r){return f0+(max(vec3(1-r),f0)-f0)*pow(clamp(1-c,0,1),5);}
vec3 cookTorrance(PBR m,vec3 l,vec3 v,vec3 rad){
  vec3 h=normalize(v+l); float ndl=max(dot(m.normal,l),0),ndv=max(dot(m.normal,v),1e-4);
  float ndh=max(dot(m.normal,h),0),vdh=max(dot(v,h),0);
  vec3 f0=mix(vec3(0.04),m.albedo,m.metallic);
  vec3 spec=D_GGX(ndh,m.roughness)*G_Smith(ndv,ndl,m.roughness)*F_Schlick(vdh,f0);
  spec/=max(4*ndv*ndl,0.0001);
  vec3 kd=(1-F_Schlick(vdh,f0))*(1-m.metallic);
  return (kd*m.albedo/PI+spec)*rad*ndl;
}
vec3 blinnPhong(PBR m,vec3 l,vec3 v,vec3 rad){
  vec3 h=normalize(v+l); float ndl=max(dot(m.normal,l),0),ndh=max(dot(m.normal,h),0),ndv=max(dot(m.normal,v),1e-4);
  vec3 f0=mix(vec3(0.04),m.albedo,m.metallic);
  float shine=mix(8,256,1-m.roughness);
  vec3 fres=F_Schlick(max(dot(h,v),0),f0);
  vec3 spec=fres*pow(ndh,shine);
  vec3 diff=m.albedo*(1-m.metallic)/PI;
  return (diff+spec)*rad*ndl;
}
vec3 rotateY(vec3 v,float a){float c=cos(a),s=sin(a);return vec3(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);}

void main(){
  PBR m;
  m.albedo=texture(albedoTex,vUv).rgb*matl.baseColorFactor.rgb;
  if(draw.tint.a>0.001)m.albedo*=draw.tint.rgb;
  m.metallic=mix(matl.pbr.x,texture(mrTex,vUv).b,matl.texMask.z);
  m.roughness=mix(matl.pbr.y,texture(mrTex,vUv).g,matl.texMask.z);
  m.roughness=clamp(m.roughness,0.045,1);
  m.ao=mix(1,texture(aoTex,vUv).r,matl.texMask.w)*matl.pbr.w;
  vec3 n=normalize(vNormal);
  if(matl.texMask.y>0.5){
    vec3 t=normalize(vTangent.xyz); t=normalize(t-n*dot(t,n));
    vec3 b=cross(n,t)*sign(vTangent.w);
    vec3 nm=texture(normalTex,vUv).rgb*2-1; nm.xy*=matl.pbr.z;
    m.normal=normalize(mat3(t,b,n)*nm);
  } else m.normal=n;
  m.emissive=matl.emissive.rgb;
  if(matl.texMask2.x>0.5)m.emissive*=texture(emissiveTex,vUv).rgb;

  vec3 v=normalize(frame.cameraPos.xyz-vWorldPos);
  vec3 result=vec3(0); int model=int(frame.params.w+0.5);
  if(frame.misc.y>0.5){
    if(frame.dirLightDir.w>0.5){
      vec3 l=normalize(frame.dirLightDir.xyz);
      vec3 rad=frame.dirLightColor.rgb*frame.dirLightColor.w;
      result+=model==0?cookTorrance(m,l,v,rad):blinnPhong(m,l,v,rad);
    }
    for(int i=0;i<4;i++){
      if(i<int(frame.misc.x)){
        vec3 lp=frame.pointPos[i].xyz; vec3 dd=lp-vWorldPos;
        float dist=length(dd); float range=frame.pointData[i].w;
        vec3 l=dd/max(dist,1e-4);
        float att=pow(max(1-dist/range,0),2);
        vec3 rad=frame.pointData[i].rgb*att;
        result+=model==0?cookTorrance(m,l,v,rad):blinnPhong(m,l,v,rad);
      }
    }
  }
  if(frame.misc.z>0.5){
    vec3 rr=reflect(-v,m.normal); rr=rotateY(rr,frame.misc.w);
    vec3 nd=rotateY(m.normal,frame.misc.w);
    vec3 f=F_SchlickRoughness(max(dot(m.normal,v),0),mix(vec3(0.04),m.albedo,m.metallic),m.roughness);
    vec3 kd=(1-f)*(1-m.metallic);
    int fi; vec2 fuv=envDirToFaceUV(nd,fi);
    vec3 diff=texture(irrAtlas,atlasUV(fi,fuv)).rgb*m.albedo;
    vec3 spec=textureLod(equirectMap, envUV(rr), clamp(m.roughness*6.0,0.0,6.0)).rgb;
    vec2 brdf=texture(brdfLUT,vec2(max(dot(m.normal,v),0),m.roughness)).rg;
    vec3 amb=(kd*diff+m.metallic*f*(brdf.x*spec+vec3(brdf.y)))*m.ao*frame.params.y;
    result+=amb;
  }
  result+=m.emissive*matl.emissive.w;
  outColor=vec4(result,1.0);
}
