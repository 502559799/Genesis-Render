#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct AnimParams
{
    float4 p0;
    float4 p1;
};

struct AnimatedVertexBuffer
{
    float data[1];
};

struct BaseVertexBuffer
{
    float data[1];
};

constant uint3 gl_WorkGroupSize [[maybe_unused]] = uint3(64u, 1u, 1u);

kernel void main0(const device BaseVertexBuffer& base_vertices [[buffer(0)]], device AnimatedVertexBuffer& animated_vertices [[buffer(1)]], device AnimParams& anim_params [[buffer(2)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint index = gl_GlobalInvocationID.x;
    uint count = uint(((device float*)&anim_params.p1)[0u]);
    if (index >= count)
    {
        return;
    }
    uint base = index * 22u;
    for (uint i = 0u; i < 22u; i++)
    {
        animated_vertices.data[base + i] = base_vertices.data[base + i];
    }
    float3 p = float3(base_vertices.data[base + 0u], base_vertices.data[base + 1u], base_vertices.data[base + 2u]);
    float time = ((device float*)&anim_params.p0)[0u];
    float amplitude = ((device float*)&anim_params.p0)[1u];
    float frequency = ((device float*)&anim_params.p0)[2u];
    float speed = ((device float*)&anim_params.p0)[3u];
    float wave = sin((p.x * frequency) + (time * speed)) * cos(((p.z * frequency) * 0.699999988079071044921875) + ((time * speed) * 0.60000002384185791015625));
    p.y += (wave * amplitude);
    p.x += ((sin(((p.y * frequency) * 0.5) + ((time * speed) * 0.4000000059604644775390625)) * amplitude) * 0.25);
    float dx = (cos((p.x * frequency) + (time * speed)) * frequency) * amplitude;
    float dz = ((cos(((p.z * frequency) * 0.699999988079071044921875) + ((time * speed) * 0.60000002384185791015625)) * frequency) * amplitude) * 0.699999988079071044921875;
    float3 n = fast::normalize(float3(-dx, 1.0, -dz));
    animated_vertices.data[base + 0u] = p.x;
    animated_vertices.data[base + 1u] = p.y;
    animated_vertices.data[base + 2u] = p.z;
    animated_vertices.data[base + 3u] = n.x;
    animated_vertices.data[base + 4u] = n.y;
    animated_vertices.data[base + 5u] = n.z;
}

