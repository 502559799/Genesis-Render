/*
 * Genesis Render - CPU IBL generation.
 *
 * Software rasterizers (llvmpipe lavapipe) mis-compile the multi-sampler,
 * high-iteration cube-map bake shaders, so irradiance / prefiltered / BRDF
 * LUT are computed deterministically on the CPU once and uploaded as
 * 2D RGBA16F face textures. Same math as the GPU shaders.
 */
#pragma once
#include <cstdint>
#include <vector>
#include <cmath>

namespace genesis::cpuibl {

constexpr float PI = 3.14159265358979323846f;

inline float saturate(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x, y, z;
    Vec3 operator+(const Vec3& o) const { return {x+o.x,y+o.y,z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x,y-o.y,z-o.z}; }
    Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 norm(Vec3 v){ float l=std::sqrt(dot(v,v)); return l>1e-12f? v*(1.0f/l):Vec3{0,0,0}; }

// face 0..5 PX,NX,PY,NY,PZ,NZ ; u,v in [0,1]
inline Vec3 faceDirection(int face, float u, float v) {
    float sc = 2.0f * u - 1.0f;
    float tc = 2.0f * v - 1.0f;
    switch (face) {
        case 0: return norm({1.0f,  tc, -sc});
        case 1: return norm({-1.0f, tc,  sc});
        case 2: return norm({ sc, 1.0f, -tc});
        case 3: return norm({ sc,-1.0f,  tc});
        case 4: return norm({ sc, tc, 1.0f});
        default:return norm({-sc, tc,-1.0f});
    }
}

// direction -> equirect pixel (nearest edge clamp)
inline void dirToEquirect(Vec3 d, uint32_t w, uint32_t h, uint32_t& x, uint32_t& y) {
    float u = std::atan2(d.z, d.x) / (2.0f * PI) + 0.5f;
    float v = 1.0f - (std::asin(saturate(d.y)) / PI + 0.5f);
    x = (uint32_t)std::lround(u * (w - 1));
    y = (uint32_t)std::lround(v * (h - 1));
}

inline Vec3 sampleEquirect(const float* eq, uint32_t w, uint32_t h, Vec3 d) {
    uint32_t x, y; dirToEquirect(d, w, h, x, y);
    const float* p = eq + ((size_t)y * w + x) * 4;
    return {p[0], p[1], p[2]};
}

// Six RGBA32F faces from an equirect source.
inline std::vector<float> buildEnvFaces(const float* eq, uint32_t ew, uint32_t eh, uint32_t S) {
    std::vector<float> out((size_t)6 * S * S * 4);
    for (int f = 0; f < 6; ++f)
        for (uint32_t y = 0; y < S; ++y)
            for (uint32_t x = 0; x < S; ++x) {
                Vec3 d = faceDirection(f, (x + 0.5f) / S, (y + 0.5f) / S);
                Vec3 c = sampleEquirect(eq, ew, eh, d);
                float* p = out.data() + ((size_t)f * S * S + (size_t)y * S + x) * 4;
                p[0]=c.x; p[1]=c.y; p[2]=c.z; p[3]=1.0f;
            }
    return out;
}

inline std::vector<float> buildIrrFaces(const float* eq, uint32_t ew, uint32_t eh, uint32_t S) {
    std::vector<float> out((size_t)6 * S * S * 4);
    const float dPhi = 0.12f, dTheta = 0.12f;
    for (int f = 0; f < 6; ++f)
        for (uint32_t y = 0; y < S; ++y)
            for (uint32_t x = 0; x < S; ++x) {
                Vec3 n = faceDirection(f, (x + 0.5f) / S, (y + 0.5f) / S);
                Vec3 up = std::fabs(n.y) < 0.999f ? Vec3{0,1,0} : Vec3{1,0,0};
                Vec3 t = norm(cross(up, n));
                Vec3 b = cross(n, t);
                Vec3 acc{0,0,0};
                float count = 0;
                for (float phi = 0; phi < 2 * PI; phi += dPhi)
                    for (float theta = 0; theta < 0.5f * PI; theta += dTheta) {
                        Vec3 sv = (t * std::cos(phi) + b * std::sin(phi)) * std::sin(theta) + n * std::cos(theta);
                        sv = norm(sv);
                        acc = acc + sampleEquirect(eq, ew, eh, sv) * (std::cos(theta) * std::sin(theta));
                        count++;
                    }
                acc = acc * (PI / count);
                float* p = out.data() + ((size_t)f * S * S + (size_t)y * S + x) * 4;
                p[0]=acc.x; p[1]=acc.y; p[2]=acc.z; p[3]=1;
            }
    return out;
}

inline float radicalVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = (bits << 8u) | (bits >> 8u);
    return bits * 2.3283064365386963e-10f;
}
inline float D_GGX(float ndh, float a) {
    float a2=a*a; float d=ndh*ndh*(a2-1)+1; return a2/ (PI*d*d);
}
inline Vec3 importanceGGX(float r1, float r2, Vec3 N, float rough) {
    float a=rough*rough;
    float phi=2*PI*r1;
    float cosT=std::sqrt((1-r2)/(1+(a*a-1)*r2));
    float sinT=std::sqrt(1-cosT*cosT);
    Vec3 H{std::cos(phi)*sinT,std::sin(phi)*sinT,cosT};
    Vec3 up=std::fabs(N.z)<0.999f?Vec3{0,0,1}:Vec3{1,0,0};
    Vec3 t=norm(cross(up,N)); Vec3 b=cross(N,t);
    return norm(t*H.x+b*H.y+N*H.z);
}

// mip levels packed contiguously
inline std::vector<float> buildPrefilter(const float* eq, uint32_t ew, uint32_t eh,
                                         uint32_t base, uint32_t mips, uint32_t samples) {
    size_t total = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t S = base >> m; total += (size_t)6 * S * S;
    }
    std::vector<float> out(total * 4);
    size_t plane = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t S = base >> m;
        float rough = (float)m / std::max(1u, mips - 1);
        for (int f = 0; f < 6; ++f)
            for (uint32_t y = 0; y < S; ++y)
                for (uint32_t x = 0; x < S; ++x) {
                    Vec3 N = faceDirection(f, (x+0.5f)/S, (y+0.5f)/S);
                    Vec3 V = N, pre{0,0,0}; float totalW = 0;
                    if (rough < 0.001f) {
                        pre = sampleEquirect(eq, ew, eh, N);
                    } else {
                        for (uint32_t i = 0; i < samples; ++i) {
                            float r1 = float(i) * 0.318309886f + 0.17f - std::floor(float(i) * 0.318309886f + 0.17f);
                            float r2 = radicalVdC(i);
                            Vec3 H = importanceGGX(r1, r2, N, rough);
                            Vec3 L = H * (2.0f * dot(V, H)) - V;
                            L = norm(L);
                            float ndl = std::max(dot(N, L), 0.0f);
                            if (ndl > 0) {
                                pre = pre + sampleEquirect(eq, ew, eh, L) * ndl;
                                totalW += ndl;
                            }
                        }
                        pre = pre * (1.0f / std::max(totalW, 1e-4f));
                    }
                    size_t idx = plane + ((size_t)f*S*S + (size_t)y*S + x);
                    float* p = &out[idx*4];
                    p[0]=pre.x;p[1]=pre.y;p[2]=pre.z;p[3]=1;
                }
        plane += (size_t)6 * S * S;
    }
    return out;
}

inline std::vector<float> buildBRDFLUT(uint32_t S, uint32_t samples) {
    std::vector<float> out((size_t)S * S * 4);
    auto schlick = [](float ndv, float k){ return ndv/(ndv*(1-k)+k); };
    for (uint32_t y=0;y<S;y++)
        for (uint32_t x=0;x<S;x++){
            float ndv=(x+0.5f)/S; float rough=(y+0.5f)/S;
            Vec3 V{std::sqrt(1-ndv*ndv),0,ndv};
            Vec3 N{0,0,1};
            float scale=0,bias=0;
            float a=rough*rough,k=(rough+1);k=k*k/8;
            for(uint32_t i=0;i<samples;i++){
                float r1=(float(i)*0.318309886f+0.17f-std::floor(float(i)*0.318309886f+0.17f));
                float r2=radicalVdC(i);
                Vec3 H=importanceGGX(r1,r2,N,rough);
                Vec3 L=H*(2*dot(V,H))-V; L=norm(L);
                float ndl=std::max(L.z,0.f),ndh=std::max(H.z,0.f),vdh=std::max(dot(V,H),0.f);
                if(ndl>0){
                    float g=schlick(ndv,k)*schlick(ndl,k);
                    float gv=g*vdh/(ndh*ndv+1e-4f);
                    float fc=std::pow(1-vdh,5);
                    scale+=(1-fc)*gv; bias+=fc*gv;
                }
            }
            float* p=&out[((size_t)y*S+x)*4];
            p[0]=scale/samples;p[1]=bias/samples;p[2]=0;p[3]=1;
        }
    return out;
}

} // namespace genesis::cpuibl
