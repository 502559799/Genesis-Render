/*
 * Genesis Render - the four public API domains
 *
 * The engine facade exposes exactly four API types.  Rendering is backed by
 * the RHI + RenderSystem, Compute by storage-buffer compute pipelines, Ray
 * Tracing by the portable compute ray tracer, and Encoder/Decoder by the
 * platform-codec facade.  Existing Engine::render()/world() accessors remain
 * as compatibility aliases, so older code keeps working unchanged.
 */
#ifndef __GENESIS_ENGINE_APIS_HPP__
#define __GENESIS_ENGINE_APIS_HPP__

#include "api/compute_api.hpp"
#include "api/encoder_decoder_api.hpp"
#include "api/ray_tracing_api.hpp"
#include "api/rendering_api.hpp"

namespace Genesis
{
    struct EngineAPIs
    {
        API::RenderingAPI *rendering = nullptr;
        API::ComputeAPI *compute = nullptr;
        API::RayTracingAPI *ray_tracing = nullptr;
        API::EncoderDecoderAPI *encoder_decoder = nullptr;
    };
} // namespace Genesis

#endif /* __GENESIS_ENGINE_APIS_HPP__ */
