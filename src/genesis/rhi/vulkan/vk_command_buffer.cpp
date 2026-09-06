/*
 * Genesis Render - Vulkan backend implementation (command buffer)
 */
#include "rhi/vulkan/vk_backend.hpp"
#include "rhi/vulkan/vk_convert.hpp"
#include "foundation_layer/genesis_log.hpp"

#include "external/vk_mem_alloc.h"

#include <algorithm>
#include <cstring>

namespace Genesis
{
    namespace RHI
    {
        //------------------------------------------------------------------
        // Layout transition helpers
        //------------------------------------------------------------------
        struct BarrierStages
        {
            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags src_access = 0;
            VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            VkAccessFlags dst_access = 0;
        };

        static BarrierStages inferBarrierStages(TextureLayout old_layout, TextureLayout new_layout, bool is_depth)
        {
            BarrierStages s;
            if (new_layout == TextureLayout::TransferDstOptimal)
            {
                s.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                s.dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            else if (new_layout == TextureLayout::TransferSrcOptimal)
            {
                s.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.src_access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                s.dst_access = VK_ACCESS_TRANSFER_READ_BIT;
            }
            else if (new_layout == TextureLayout::ShaderReadOnlyOptimal)
            {
                s.src_stage = is_depth ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.src_access = is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                        : (VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
                s.dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                s.dst_access = VK_ACCESS_SHADER_READ_BIT;
            }
            else if (new_layout == TextureLayout::ColorAttachmentOptimal)
            {
                s.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.src_access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                s.dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
            else if (new_layout == TextureLayout::DepthStencilAttachmentOptimal)
            {
                s.src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                s.src_access = 0;
                s.dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                s.dst_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            else if (new_layout == TextureLayout::DepthStencilReadOnlyOptimal)
            {
                s.src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                s.src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                s.dst_access = VK_ACCESS_SHADER_READ_BIT;
            }
            else if (new_layout == TextureLayout::PresentSrc)
            {
                s.src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                s.src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                s.dst_access = 0;
            }
            else if (new_layout == TextureLayout::General)
            {
                s.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                s.dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                s.dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            }
            return s;
        }

        //==================================================================
        void VkCommandBufferRHI::begin(CommandBufferUsage usage)
        {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = (usage == CommandBufferUsage::OneTimeSubmit) ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0;
            if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS)
                GEN_LOG_ERROR("[VK] vkBeginCommandBuffer failed");
        }

        void VkCommandBufferRHI::end()
        {
            if (inside_pass)
            {
                GEN_LOG_WARN("[VK] command buffer ended while a render pass was still open");
                vkCmdEndRenderPass(cmd);
                inside_pass = false;
            }
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
                GEN_LOG_ERROR("[VK] vkEndCommandBuffer failed");
        }

        void VkCommandBufferRHI::beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb, const RenderPassBeginInfo &info)
        {
            VkRenderPassRHI *rp = static_cast<VkRenderPassRHI *>(pass);
            VkFramebufferRHI *fbk = static_cast<VkFramebufferRHI *>(fb);
            if (!rp || !fbk) return;
            current_pass = rp->render_pass;
            inside_pass = true;

            std::vector<VkClearValue> clears;
            const RenderPassDesc &rpd = rp->desc_;
            for (uint32_t i = 0; i < rpd.attachment_count; ++i)
            {
                const AttachmentDesc &ad = rpd.attachments[i];
                RenderPassBeginInfo::ClearValue cv;
                if (i < info.clear_count) cv = info.clears[i];
                VkClearValue value{};
                if (ad.is_depth_stencil)
                {
                    value.depthStencil.depth = cv.depth;
                    value.depthStencil.stencil = cv.stencil;
                }
                else
                {
                    value.color.float32[0] = cv.color[0];
                    value.color.float32[1] = cv.color[1];
                    value.color.float32[2] = cv.color[2];
                    value.color.float32[3] = cv.color[3];
                }
                clears.push_back(value);
            }

            VkRenderPassBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            bi.renderPass = rp->render_pass;
            bi.framebuffer = fbk->framebuffer;
            bi.renderArea.offset = {0, 0};
            bi.renderArea.extent = {fbk->w, fbk->h};
            bi.clearValueCount = (uint32_t)clears.size();
            bi.pClearValues = clears.empty() ? nullptr : clears.data();
            vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        }

        void VkCommandBufferRHI::endRenderPass()
        {
            if (!inside_pass) return;
            vkCmdEndRenderPass(cmd);
            inside_pass = false;
            current_pass = VK_NULL_HANDLE;
        }

        void VkCommandBufferRHI::bindPipeline(RHIPipeline *pipeline)
        {
            VkPipelineRHI *p = static_cast<VkPipelineRHI *>(pipeline);
            if (!p) return;
            current_pipeline = p;
            current_bind_point = p->compute_ ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkCmdBindPipeline(cmd, current_bind_point, p->pipeline);
        }

        void VkCommandBufferRHI::bindVertexBuffer(RHIBuffer *buffer, uint64_t offset)
        {
            VkBufferRHI *b = static_cast<VkBufferRHI *>(buffer);
            if (!b) return;
            VkBuffer buffers[] = {b->buffer};
            VkDeviceSize offsets[] = {(VkDeviceSize)offset};
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        }

        void VkCommandBufferRHI::bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset)
        {
            VkBufferRHI *b = static_cast<VkBufferRHI *>(buffer);
            if (!b) return;
            bound_index_type = type;
            vkCmdBindIndexBuffer(cmd, b->buffer, (VkDeviceSize)offset,
                                 type == IndexType::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
        }

        void VkCommandBufferRHI::bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets, uint32_t set_count,
                                                    const uint32_t *dynamic_offsets, uint32_t offset_count)
        {
            VkPipelineRHI *p = static_cast<VkPipelineRHI *>(pipeline);
            if (!p || set_count == 0) return;
            std::vector<VkDescriptorSet> vk_sets;
            vk_sets.reserve(set_count);
            for (uint32_t i = 0; i < set_count; ++i)
            {
                VkDescriptorSetRHI *s = static_cast<VkDescriptorSetRHI *>(sets[i]);
                vk_sets.push_back(s ? s->set : VK_NULL_HANDLE);
            }
            vkCmdBindDescriptorSets(cmd, current_bind_point, p->layout, 0, (uint32_t)vk_sets.size(), vk_sets.data(),
                                    offset_count, dynamic_offsets);
        }

        void VkCommandBufferRHI::setPushConstants(const void *data, uint32_t size)
        {
            if (!current_pipeline || size == 0 || !data) return;
            const ResourceLayoutDesc &rl = current_pipeline->desc_.resource_layout;
            if (rl.push_constant_size == 0) return;
            vkCmdPushConstants(cmd, current_pipeline->layout, vkStageMask(rl.push_constant_stages), 0, size, data);
        }

        void VkCommandBufferRHI::setViewport(const Viewport &vp)
        {
            VkViewport v{};
            v.x = vp.x;
            v.y = vp.y;
            v.width = vp.width;
            v.height = vp.height;
            v.minDepth = vp.min_depth;
            v.maxDepth = vp.max_depth;
            vkCmdSetViewport(cmd, 0, 1, &v);
        }

        void VkCommandBufferRHI::setScissor(const Rect2D &rect)
        {
            VkRect2D r{};
            r.offset = {rect.x, rect.y};
            r.extent = {rect.width, rect.height};
            vkCmdSetScissor(cmd, 0, 1, &r);
        }

        void VkCommandBufferRHI::draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance)
        {
            vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
        }

        void VkCommandBufferRHI::drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                                             uint32_t instance_count, uint32_t first_instance)
        {
            vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
        }

        void VkCommandBufferRHI::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z)
        {
            vkCmdDispatch(cmd, group_x, group_y, group_z);
        }

        void VkCommandBufferRHI::copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst, uint64_t dst_offset, uint64_t size)
        {
            VkBufferRHI *s = static_cast<VkBufferRHI *>(src);
            VkBufferRHI *d = static_cast<VkBufferRHI *>(dst);
            if (!s || !d) return;
            VkBufferCopy region{};
            region.srcOffset = src_offset;
            region.dstOffset = dst_offset;
            region.size = size;
            vkCmdCopyBuffer(cmd, s->buffer, d->buffer, 1, &region);
        }

        void VkCommandBufferRHI::copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &region)
        {
            VkBufferRHI *s = static_cast<VkBufferRHI *>(src);
            VkTextureRHI *d = static_cast<VkTextureRHI *>(dst);
            if (!s || !d) return;
            VkBufferImageCopy r{};
            r.bufferOffset = region.buffer_offset;
            r.bufferRowLength = region.buffer_row_length;
            r.bufferImageHeight = region.buffer_image_height;
            r.imageSubresource.aspectMask = vkAspectMask(d->format());
            r.imageSubresource.mipLevel = region.mip_level;
            r.imageSubresource.baseArrayLayer = region.base_layer;
            r.imageSubresource.layerCount = region.layer_count;
            r.imageOffset = {(int32_t)region.offset_x, (int32_t)region.offset_y, (int32_t)region.offset_z};
            r.imageExtent = {region.width, region.height, std::max(1u, region.depth)};
            vkCmdCopyBufferToImage(cmd, s->buffer, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        }

        void VkCommandBufferRHI::copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &region)
        {
            VkTextureRHI *s = static_cast<VkTextureRHI *>(src);
            VkBufferRHI *d = static_cast<VkBufferRHI *>(dst);
            if (!s || !d) return;
            VkBufferImageCopy r{};
            r.bufferOffset = region.buffer_offset;
            r.bufferRowLength = region.buffer_row_length;
            r.bufferImageHeight = region.buffer_image_height;
            r.imageSubresource.aspectMask = vkAspectMask(s->format());
            r.imageSubresource.mipLevel = region.mip_level;
            r.imageSubresource.baseArrayLayer = region.base_layer;
            r.imageSubresource.layerCount = region.layer_count;
            r.imageOffset = {(int32_t)region.offset_x, (int32_t)region.offset_y, (int32_t)region.offset_z};
            r.imageExtent = {region.width, region.height, std::max(1u, region.depth)};
            vkCmdCopyImageToBuffer(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->buffer, 1, &r);
        }

        void VkCommandBufferRHI::pipelineBarrier(const TextureBarrier *barriers, uint32_t count)
        {
            if (!barriers || count == 0) return;
            std::vector<VkImageMemoryBarrier> vk_barriers;
            vk_barriers.reserve(count);
            VkPipelineStageFlags src_stage = 0, dst_stage = 0;

            for (uint32_t i = 0; i < count; ++i)
            {
                const TextureBarrier &b = barriers[i];
                VkTextureRHI *t = static_cast<VkTextureRHI *>(b.texture);
                if (!t) continue;
                bool is_depth = pixelFormatIsDepth(t->format());
                BarrierStages stages = inferBarrierStages(b.old_layout, b.new_layout, is_depth);
                src_stage |= stages.src_stage;
                dst_stage |= stages.dst_stage;

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = vkTextureLayout(b.old_layout);
                barrier.newLayout = vkTextureLayout(b.new_layout);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = t->image;
                barrier.subresourceRange.aspectMask = vkAspectMask(t->format());
                barrier.subresourceRange.baseMipLevel = b.base_mip;
                barrier.subresourceRange.levelCount = (b.mip_count == ~0u) ? (int32_t)t->mipLevels() - (int32_t)b.base_mip : (int32_t)b.mip_count;
                barrier.subresourceRange.baseArrayLayer = b.base_layer;
                barrier.subresourceRange.layerCount = (b.layer_count == ~0u) ? (int32_t)t->layerCount() - (int32_t)b.base_layer : (int32_t)b.layer_count;
                barrier.srcAccessMask = stages.src_access;
                barrier.dstAccessMask = stages.dst_access;
                vk_barriers.push_back(barrier);
                t->setCurrentLayout(b.new_layout);
            }
            if (vk_barriers.empty()) return;
            if (!src_stage) src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            if (!dst_stage) dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, (uint32_t)vk_barriers.size(), vk_barriers.data());
        }

        void VkCommandBufferRHI::generateMipmaps(RHITexture *texture)
        {
            VkTextureRHI *t = static_cast<VkTextureRHI *>(texture);
            if (!t) return;
            int32_t w = (int32_t)t->width();
            int32_t h = (int32_t)t->height();
            uint32_t layers = t->layerCount();

            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.image = t->image;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseArrayLayer = layer;
                barrier.subresourceRange.layerCount = 1;
                barrier.subresourceRange.levelCount = 1;

                int32_t mip_w = w, mip_h = h;
                for (uint32_t mip = 1; mip < t->mipLevels(); ++mip)
                {
                    // mip-1: TRANSFER_SRC -> ready to be read
                    barrier.subresourceRange.baseMipLevel = mip - 1;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);

                    VkImageBlit blit{};
                    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.srcSubresource.mipLevel = mip - 1;
                    blit.srcSubresource.baseArrayLayer = layer;
                    blit.srcSubresource.layerCount = 1;
                    blit.srcOffsets[0] = {0, 0, 0};
                    blit.srcOffsets[1] = {mip_w, mip_h, 1};
                    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.dstSubresource.mipLevel = mip;
                    blit.dstSubresource.baseArrayLayer = layer;
                    blit.dstSubresource.layerCount = 1;
                    blit.dstOffsets[0] = {0, 0, 0};
                    blit.dstOffsets[1] = {mip_w > 1 ? mip_w / 2 : 1, mip_h > 1 ? mip_h / 2 : 1, 1};
                    vkCmdBlitImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                    // mip-1 -> SHADER_READ_ONLY (final state)
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);

                    mip_w = std::max(1, mip_w / 2);
                    mip_h = std::max(1, mip_h / 2);
                }

                // last mip
                barrier.subresourceRange.baseMipLevel = t->mipLevels() - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);
            }
            t->setCurrentLayout(TextureLayout::ShaderReadOnlyOptimal);
        }

        void VkCommandBufferRHI::blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                                           uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter)
        {
            VkTextureRHI *s = static_cast<VkTextureRHI *>(src);
            VkTextureRHI *d = static_cast<VkTextureRHI *>(dst);
            if (!s || !d) return;

            TextureBarrier src_barrier{};
            src_barrier.texture = s;
            src_barrier.old_layout = s->currentLayout();
            src_barrier.new_layout = TextureLayout::TransferSrcOptimal;
            src_barrier.base_mip = src_mip;
            src_barrier.mip_count = 1;
            src_barrier.base_layer = 0;
            src_barrier.layer_count = ~0u;
            pipelineBarrier(&src_barrier, 1);

            TextureBarrier dst_barrier{};
            dst_barrier.texture = d;
            dst_barrier.old_layout = d->currentLayout();
            dst_barrier.new_layout = TextureLayout::TransferDstOptimal;
            dst_barrier.base_mip = dst_mip;
            dst_barrier.mip_count = 1;
            dst_barrier.base_layer = 0;
            dst_barrier.layer_count = ~0u;
            pipelineBarrier(&dst_barrier, 1);

            uint32_t sw = std::max(1u, s->width() >> src_mip);
            uint32_t sh = std::max(1u, s->height() >> src_mip);
            uint32_t dw = std::max(1u, d->width() >> dst_mip);
            uint32_t dh = std::max(1u, d->height() >> dst_mip);

            for (uint32_t layer = 0; layer < layer_count; ++layer)
            {
                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = vkAspectMask(s->format());
                blit.srcSubresource.mipLevel = src_mip;
                blit.srcSubresource.baseArrayLayer = src_layer + layer;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {(int32_t)sw, (int32_t)sh, 1};
                blit.dstSubresource.aspectMask = vkAspectMask(d->format());
                blit.dstSubresource.mipLevel = dst_mip;
                blit.dstSubresource.baseArrayLayer = dst_layer + layer;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {(int32_t)dw, (int32_t)dh, 1};
                vkCmdBlitImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                               linear_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
            }
        }

        bool VkCommandBufferRHI::readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer, void *dst, uint32_t dst_size_bytes)
        {
            VkTextureRHI *t = static_cast<VkTextureRHI *>(src);
            if (!t || !dst || !device) return false;
            if (inside_pass)
            {
                GEN_LOG_ERROR("[VK] readImagePixels called inside a render pass");
                return false;
            }
            uint32_t w = std::max(1u, t->width() >> mip);
            uint32_t h = std::max(1u, t->height() >> mip);
            uint32_t bpp = pixelFormatBytesPerPixel(t->format());
            VkDeviceSize size = (VkDeviceSize)w * h * bpp;
            if (dst_size_bytes < size)
            {
                GEN_LOG_ERROR("[VK] readImagePixels: destination too small (%u < %llu)", dst_size_bytes, (unsigned long long)size);
                return false;
            }

            BufferDesc staging_desc;
            staging_desc.size = size;
            staging_desc.usage = (uint32_t)BufferUsageFlagBits::TransferDst;
            staging_desc.memory_usage = MemoryUsage::GpuToCpu;
            staging_desc.name = "readback-staging";
            VkBufferRHI *staging = static_cast<VkBufferRHI *>(device->createBuffer(staging_desc));
            if (!staging) return false;

            TextureLayout previous = t->currentLayout();
            VkCommandBuffer one_time = device->beginSingleTimeCommands();

            TextureBarrier to_src{};
            to_src.texture = t;
            to_src.old_layout = previous;
            to_src.new_layout = TextureLayout::TransferSrcOptimal;
            to_src.base_mip = mip;
            to_src.mip_count = 1;
            to_src.base_layer = layer;
            to_src.layer_count = 1;
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = vkTextureLayout(previous);
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = t->image;
                barrier.subresourceRange.aspectMask = vkAspectMask(t->format());
                barrier.subresourceRange.baseMipLevel = mip;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = layer;
                barrier.subresourceRange.layerCount = 1;
                BarrierStages st = inferBarrierStages(previous, TextureLayout::TransferSrcOptimal, pixelFormatIsDepth(t->format()));
                barrier.srcAccessMask = st.src_access;
                barrier.dstAccessMask = st.dst_access;
                vkCmdPipelineBarrier(one_time, st.src_stage, st.dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.imageSubresource.aspectMask = vkAspectMask(t->format());
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(one_time, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);

            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = vkTextureLayout(previous);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = t->image;
                barrier.subresourceRange.aspectMask = vkAspectMask(t->format());
                barrier.subresourceRange.baseMipLevel = mip;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = layer;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(one_time, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            device->endSingleTimeCommands(one_time);

            void *mapped = nullptr;
            if (staging->mapped) mapped = staging->mapped;
            else vmaMapMemory(vmaAlloc(device->vmaAllocator()),
                              vmaAllocOf(staging->allocation), &mapped);
            if (mapped) std::memcpy(dst, mapped, (size_t)size);
            if (mapped && !staging->mapped)
                vmaUnmapMemory(vmaAlloc(device->vmaAllocator()),
                               vmaAllocOf(staging->allocation));
            device->destroyBuffer(staging);
            return mapped != nullptr;
        }

    } // namespace RHI
} // namespace Genesis
