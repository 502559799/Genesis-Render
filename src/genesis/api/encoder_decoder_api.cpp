/*
 * Genesis Render - Encoder & Decoder API implementation
 *
 * H.264 and media playback use libavcodec/libavformat/libswscale/libswresample
 * directly.  No external process is started.
 */
#include "api/encoder_decoder_api.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "media/media_player.hpp"
#include "resource/resource_types.hpp"

#include <zlib.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifndef GENESIS_ENABLE_FFMPEG
#define GENESIS_ENABLE_FFMPEG 0
#endif

#if GENESIS_ENABLE_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace Genesis
{
    namespace API
    {
        namespace
        {
            constexpr char kMagic[8] = {'G', 'S', 'E', 'Q', '0', '0', '0', '1'};
            constexpr uint32_t kVersion = 1;

            uint32_t readU32(const uint8_t *p)
            {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            }

            void appendU32(std::vector<uint8_t> &out, uint32_t value)
            {
                out.push_back((uint8_t)value);
                out.push_back((uint8_t)(value >> 8));
                out.push_back((uint8_t)(value >> 16));
                out.push_back((uint8_t)(value >> 24));
            }

            struct TempDir
            {
                std::filesystem::path path;
                TempDir()
                {
                    static uint32_t counter = 0;
                    path = std::filesystem::temp_directory_path() /
                           ("genesis_codec_" + std::to_string((unsigned long)getpid()) + "_" +
                            std::to_string(counter++));
                    std::error_code ec;
                    std::filesystem::create_directories(path, ec);
                }
                ~TempDir()
                {
                    std::error_code ec;
                    std::filesystem::remove_all(path, ec);
                }
            };

#if GENESIS_ENABLE_FFMPEG
            std::string avErrorString(int code)
            {
                char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(code, buffer, sizeof(buffer));
                return std::string(buffer);
            }

            bool hwDeviceUsable(AVHWDeviceType type, const char *device, std::string &detail)
            {
                AVBufferRef *context = nullptr;
                const int previous_level = av_log_get_level();
                av_log_set_level(AV_LOG_QUIET);
                const int ret = av_hwdevice_ctx_create(&context, type, device, nullptr, 0);
                av_log_set_level(previous_level);
                if (ret < 0)
                {
                    detail = "no usable device: " + avErrorString(ret);
                    return false;
                }
                av_buffer_unref(&context);
                detail = "device available";
                return true;
            }

#endif
        } // namespace

        bool EncoderDecoderAPI::initialize()
        {
#if GENESIS_ENABLE_FFMPEG
            av_log_set_level(AV_LOG_ERROR);
#endif
            probe();
#if GENESIS_ENABLE_FFMPEG
            GEN_LOG_INFO("[Codec] libavcodec direct mode (%s), %zu codecs", avcodec_version() ? av_version_info() : "?",
                         codecs_.size());
#else
            GEN_LOG_WARN("[Codec] built without FFmpeg - only Raw/PNG are available");
#endif
            return true;
        }

        void EncoderDecoderAPI::probe()
        {
            codecs_.clear();
            ffmpeg_available_ = GENESIS_ENABLE_FFMPEG != 0;
            has_dri_ = std::filesystem::exists("/dev/dri/renderD128");

            codecs_.push_back({"raw", false, true, "zlib-compressed RGBA sequence (.gseq)"});
            codecs_.push_back({"png", false, true, "lossless PNG frames (.gseq)"});
#if GENESIS_ENABLE_FFMPEG
            auto add = [&](const char *name, bool hardware, bool available, const std::string &detail)
            {
                if (avcodec_find_encoder_by_name(name))
                    codecs_.push_back({name, hardware, available, detail});
            };
            add("libx264", false, avcodec_find_encoder_by_name("libx264") != nullptr, "software H.264");
            {
                std::string detail;
                const bool usable = has_dri_ && hwDeviceUsable(AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", detail);
                add("h264_vaapi", true, usable, usable ? "VA-API" : detail);
            }
            {
                std::string detail;
                const bool usable = hwDeviceUsable(AV_HWDEVICE_TYPE_CUDA, nullptr, detail);
                add("h264_nvenc", true, usable, usable ? "NVIDIA NVENC" : detail);
            }
            {
                // QSV needs a Media SDK / oneVPL device; report it but do not
                // auto-select it until the hw-frame path is wired.
                add("h264_qsv", true, false, "encoder built, hw-frame path not wired");
            }
            {
                std::string detail;
                const bool usable = hwDeviceUsable(AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, detail);
                add("h264_videotoolbox", true, usable, usable ? "Apple VideoToolbox" : detail);
            }
#else
            codecs_.push_back({"h264", false, false, "built without FFmpeg"});
#endif
        }

        std::string EncoderDecoderAPI::chooseH264Encoder(const CodecOptions &options) const
        {
            auto available = [&](const char *name) -> bool
            {
                for (const CodecInfo &info : codecs_)
                    if (info.name == name) return info.available;
                return false;
            };
            const bool hardware = options.prefer_hardware && options.hardware != HardwarePreference::Disabled;
            if (hardware)
            {
                switch (options.hardware)
                {
                case HardwarePreference::NVENC: if (available("h264_nvenc")) return "h264_nvenc"; break;
                case HardwarePreference::VAAPI: if (available("h264_vaapi")) return "h264_vaapi"; break;
                case HardwarePreference::QSV: if (available("h264_qsv")) return "h264_qsv"; break;
                case HardwarePreference::VideoToolbox:
                    if (available("h264_videotoolbox")) return "h264_videotoolbox";
                    break;
                case HardwarePreference::Auto:
                default:
                    if (available("h264_videotoolbox")) return "h264_videotoolbox";
                    if (available("h264_nvenc")) return "h264_nvenc";
                    if (available("h264_vaapi")) return "h264_vaapi";
                    if (available("h264_qsv")) return "h264_qsv";
                    break;
                }
            }
            return available("libx264") ? "libx264" : "";
        }

        bool EncoderDecoderAPI::encodeFrame(const FrameImage &frame, std::vector<uint8_t> &out, Codec codec,
                                            std::string *error)
        {
            const auto start = std::chrono::steady_clock::now();
            out.clear();
            if (!frame.valid())
            {
                if (error) *error = "invalid frame";
                return false;
            }
            if (codec == Codec::PNG)
            {
                TempDir temp;
                const std::filesystem::path file = temp.path / "frame.png";
                if (!ResourceIO::writeImagePNG(file.string(), frame.width, frame.height, frame.rgba.data()))
                {
                    if (error) *error = "PNG encode failed";
                    return false;
                }
                if (!ResourceIO::readFile(file.string(), out))
                {
                    if (error) *error = "PNG readback failed";
                    return false;
                }
            }
            else
            {
                uLongf bound = compressBound((uLong)frame.rgba.size());
                std::vector<uint8_t> compressed(bound);
                if (compress2(compressed.data(), &bound, frame.rgba.data(), (uLong)frame.rgba.size(), 6) != Z_OK)
                {
                    if (error) *error = "zlib compression failed";
                    return false;
                }
                compressed.resize(bound);
                out.reserve(20 + compressed.size());
                out.insert(out.end(), kMagic, kMagic + 8);
                appendU32(out, frame.width);
                appendU32(out, frame.height);
                appendU32(out, (uint32_t)frame.rgba.size());
                out.insert(out.end(), compressed.begin(), compressed.end());
            }
            stats_.encode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            stats_.backend = codec == Codec::PNG ? "png" : "raw";
            stats_.frames = 1;
            stats_.width = frame.width;
            stats_.height = frame.height;
            return true;
        }

        bool EncoderDecoderAPI::decodeFrame(const uint8_t *data, size_t size, FrameImage &out, std::string *error)
        {
            const auto start = std::chrono::steady_clock::now();
            out = FrameImage{};
            if (!data || size < 8)
            {
                if (error) *error = "empty frame payload";
                return false;
            }
            if (std::memcmp(data, kMagic, 8) == 0)
            {
                if (size < 20)
                {
                    if (error) *error = "truncated raw frame";
                    return false;
                }
                out.width = readU32(data + 8);
                out.height = readU32(data + 12);
                const uint32_t raw_size = readU32(data + 16);
                out.rgba.resize(raw_size);
                uLongf dest = raw_size;
                if (uncompress(out.rgba.data(), &dest, data + 20, (uLong)(size - 20)) != Z_OK || dest != raw_size)
                {
                    if (error) *error = "zlib decompression failed";
                    return false;
                }
                stats_.backend = "raw";
            }
            else
            {
                std::shared_ptr<ImageData> image = ResourceIO::loadImageFromMemory(data, size, "codec_frame");
                if (!image)
                {
                    if (error) *error = "image decode failed (expected PNG or raw .gseq frame)";
                    return false;
                }
                out.width = image->width;
                out.height = image->height;
                out.rgba = image->asRGBA8();
                stats_.backend = "png";
            }
            stats_.decode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            stats_.frames = 1;
            stats_.width = out.width;
            stats_.height = out.height;
            return out.valid();
        }

        bool EncoderDecoderAPI::encodeSequence(const std::vector<FrameImage> &frames, const std::string &path,
                                                const CodecOptions &options, std::string *error)
        {
            const auto start = std::chrono::steady_clock::now();
            stats_ = CodecStats{};
            if (frames.empty())
            {
                if (error) *error = "no frames to encode";
                return false;
            }
            stats_.frames = (uint32_t)frames.size();
            stats_.width = frames[0].width;
            stats_.height = frames[0].height;
            for (const FrameImage &frame : frames)
                if (!frame.valid() || frame.width != stats_.width || frame.height != stats_.height)
                {
                    if (error) *error = "frames must be valid and have the same size";
                    return false;
                }

            if (options.codec == Codec::H264)
            {
                if (!ffmpeg_available_)
                {
                    if (error) *error = "Genesis was built without FFmpeg";
                    return false;
                }
                if (!encodeH264(frames, path, options, error)) return false;
            }
            else
            {
                std::vector<uint8_t> out;
                out.insert(out.end(), kMagic, kMagic + 8);
                appendU32(out, kVersion);
                appendU32(out, stats_.width);
                appendU32(out, stats_.height);
                appendU32(out, (uint32_t)frames.size());
                appendU32(out, (uint32_t)std::max(1, options.fps));
                appendU32(out, (uint32_t)options.codec);
                std::vector<std::vector<uint8_t>> payloads(frames.size());
                for (size_t i = 0; i < frames.size(); ++i)
                {
                    std::string frame_error;
                    if (!encodeFrame(frames[i], payloads[i], options.codec, &frame_error))
                    {
                        if (error) *error = frame_error;
                        return false;
                    }
                    appendU32(out, (uint32_t)payloads[i].size());
                }
                for (const auto &payload : payloads) out.insert(out.end(), payload.begin(), payload.end());
                if (!ResourceIO::writeFile(path, out.data(), out.size()))
                {
                    if (error) *error = "cannot write " + path;
                    return false;
                }
                stats_.backend = options.codec == Codec::PNG ? "png" : "raw";
            }
            stats_.encode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return true;
        }

        bool EncoderDecoderAPI::decodeSequence(const std::string &path, std::vector<FrameImage> &frames,
                                                std::string *error)
        {
            const auto start = std::chrono::steady_clock::now();
            frames.clear();
            stats_ = CodecStats{};
            std::vector<uint8_t> blob;
            if (!ResourceIO::readFile(path, blob) || blob.size() < 8)
            {
                if (error) *error = "cannot read " + path;
                return false;
            }
            if (std::memcmp(blob.data(), kMagic, 8) == 0)
            {
                if (blob.size() < 32)
                {
                    if (error) *error = "truncated sequence header";
                    return false;
                }
                const uint32_t version = readU32(blob.data() + 8);
                if (version != kVersion)
                {
                    if (error) *error = "unsupported sequence version";
                    return false;
                }
                const uint32_t width = readU32(blob.data() + 12);
                const uint32_t height = readU32(blob.data() + 16);
                const uint32_t count = readU32(blob.data() + 20);
                size_t cursor = 32;
                if (count > 100000 || cursor + (size_t)count * 4 > blob.size())
                {
                    if (error) *error = "truncated sequence header";
                    return false;
                }
                std::vector<uint32_t> sizes(count);
                for (uint32_t i = 0; i < count; ++i)
                {
                    sizes[i] = readU32(blob.data() + cursor);
                    cursor += 4;
                }
                stats_.width = width;
                stats_.height = height;
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (cursor + sizes[i] > blob.size())
                    {
                        if (error) *error = "truncated sequence payload";
                        return false;
                    }
                    FrameImage frame;
                    if (!decodeFrame(blob.data() + cursor, sizes[i], frame, error)) return false;
                    if (frame.width != width || frame.height != height)
                    {
                        if (error) *error = "frame size mismatch";
                        return false;
                    }
                    frames.push_back(std::move(frame));
                    cursor += sizes[i];
                }
                stats_.backend = "raw/png sequence";
            }
            else
            {
                if (!ffmpeg_available_)
                {
                    if (error) *error = "Genesis was built without FFmpeg";
                    return false;
                }
                if (!decodeH264(path, frames, error)) return false;
            }
            stats_.frames = (uint32_t)frames.size();
            stats_.decode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return !frames.empty();
        }

        //------------------------------------------------------------------
        // Direct libavcodec / libavformat H.264 encode
        //------------------------------------------------------------------
        bool EncoderDecoderAPI::encodeH264(const std::vector<FrameImage> &frames, const std::string &path,
                                            const CodecOptions &options, std::string *error)
        {
#if !GENESIS_ENABLE_FFMPEG
            (void)frames; (void)path; (void)options;
            if (error) *error = "Genesis was built without FFmpeg";
            return false;
#else
            const std::string encoder_name = chooseH264Encoder(options);
            if (encoder_name.empty())
            {
                if (error) *error = "no H.264 encoder available";
                return false;
            }
            const AVCodec *codec = avcodec_find_encoder_by_name(encoder_name.c_str());
            if (!codec)
            {
                if (error) *error = "cannot find encoder " + encoder_name;
                return false;
            }

            AVFormatContext *format = nullptr;
            int ret = avformat_alloc_output_context2(&format, nullptr, nullptr, path.c_str());
            if (ret < 0 || !format)
            {
                if (error) *error = "cannot create output context: " + avErrorString(ret);
                return false;
            }
            AVStream *stream = avformat_new_stream(format, nullptr);
            AVCodecContext *context = avcodec_alloc_context3(codec);
            if (!stream || !context)
            {
                if (error) *error = "cannot allocate encoder";
                if (context) avcodec_free_context(&context);
                avformat_free_context(format);
                return false;
            }

            const int width = (int)frames[0].width & ~1;
            const int height = (int)frames[0].height & ~1;
            const int fps = std::max(1, options.fps);
            context->codec_type = AVMEDIA_TYPE_VIDEO;
            context->codec_id = codec->id;
            context->width = width;
            context->height = height;
            context->time_base = AVRational{1, fps};
            context->framerate = AVRational{fps, 1};
            context->gop_size = fps;
            context->max_b_frames = 0;
            context->bit_rate = 2 * 1000 * 1000;
            // Fixed formats that every H.264 encoder accepts.  Avoid querying
            // AVCodec::pix_fmts, which was removed in FFmpeg 7.
            context->pix_fmt = (encoder_name == "h264_videotoolbox") ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;

            const bool vaapi = encoder_name == "h264_vaapi";
            AVBufferRef *hw_device = nullptr;
            AVBufferRef *hw_frames = nullptr;
            if (vaapi)
            {
                ret = av_hwdevice_ctx_create(&hw_device, AV_HWDEVICE_TYPE_VAAPI,
                                             has_dri_ ? "/dev/dri/renderD128" : nullptr, nullptr, 0);
                if (ret < 0)
                {
                    if (error) *error = "VA-API device: " + avErrorString(ret);
                    avcodec_free_context(&context);
                    avformat_free_context(format);
                    return false;
                }
                hw_frames = av_hwframe_ctx_alloc(hw_device);
                AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames->data;
                frames_ctx->format = AV_PIX_FMT_VAAPI;
                frames_ctx->sw_format = AV_PIX_FMT_NV12;
                frames_ctx->width = width;
                frames_ctx->height = height;
                frames_ctx->initial_pool_size = 20;
                if (av_hwframe_ctx_init(hw_frames) < 0)
                {
                    if (error) *error = "cannot initialise VA-API frames";
                    av_buffer_unref(&hw_frames);
                    av_buffer_unref(&hw_device);
                    avcodec_free_context(&context);
                    avformat_free_context(format);
                    return false;
                }
                context->pix_fmt = AV_PIX_FMT_VAAPI;
                context->hw_frames_ctx = av_buffer_ref(hw_frames);
            }

            if (encoder_name == "libx264")
            {
                av_opt_set(context->priv_data, "preset", "veryfast", 0);
                av_opt_set_int(context->priv_data, "crf", options.quality, 0);
            }
            else if (encoder_name == "h264_nvenc")
            {
                av_opt_set(context->priv_data, "preset", "p4", 0);
                av_opt_set_int(context->priv_data, "cq", options.quality, 0);
            }
            else if (encoder_name == "h264_videotoolbox")
            {
                context->bit_rate = 3 * 1000 * 1000;
            }

            ret = avcodec_open2(context, codec, nullptr);
            if (ret < 0)
            {
                if (error) *error = "cannot open encoder " + encoder_name + ": " + avErrorString(ret);
                if (context->hw_frames_ctx) av_buffer_unref(&context->hw_frames_ctx);
                if (hw_frames) av_buffer_unref(&hw_frames);
                if (hw_device) av_buffer_unref(&hw_device);
                avcodec_free_context(&context);
                avformat_free_context(format);
                return false;
            }
            avcodec_parameters_from_context(stream->codecpar, context);
            stream->time_base = context->time_base;

            ret = avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                if (error) *error = "cannot open output: " + avErrorString(ret);
                if (context->hw_frames_ctx) av_buffer_unref(&context->hw_frames_ctx);
                if (hw_frames) av_buffer_unref(&hw_frames);
                if (hw_device) av_buffer_unref(&hw_device);
                avcodec_free_context(&context);
                avformat_free_context(format);
                return false;
            }
            ret = avformat_write_header(format, nullptr);
            if (ret < 0)
            {
                if (error) *error = "cannot write header: " + avErrorString(ret);
                avio_closep(&format->pb);
                avcodec_free_context(&context);
                avformat_free_context(format);
                return false;
            }

            const AVPixelFormat source_format = vaapi ? AV_PIX_FMT_NV12 : context->pix_fmt;
            SwsContext *scaler = sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height, source_format,
                                                SWS_BILINEAR, nullptr, nullptr, nullptr);
            bool ok = scaler != nullptr;
            auto writePackets = [&]() -> bool
            {
                AVPacket *packet = av_packet_alloc();
                while (packet)
                {
                    const int receive = avcodec_receive_packet(context, packet);
                    if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) break;
                    if (receive < 0) { ok = false; break; }
                    av_packet_rescale_ts(packet, context->time_base, stream->time_base);
                    packet->stream_index = stream->index;
                    if (av_interleaved_write_frame(format, packet) < 0) ok = false;
                    av_packet_unref(packet);
                }
                av_packet_free(&packet);
                return ok;
            };

            for (size_t i = 0; ok && i < frames.size(); ++i)
            {
                AVFrame *picture = av_frame_alloc();
                picture->format = source_format;
                picture->width = width;
                picture->height = height;
                if (av_frame_get_buffer(picture, 32) < 0) { ok = false; av_frame_free(&picture); break; }
                const uint8_t *src[4] = {frames[i].rgba.data(), nullptr, nullptr, nullptr};
                const int src_lines[4] = {(int)frames[i].width * 4, 0, 0, 0};
                sws_scale(scaler, src, src_lines, 0, height, picture->data, picture->linesize);

                AVFrame *send_frame = picture;
                AVFrame *hw_frame = nullptr;
                if (vaapi)
                {
                    hw_frame = av_frame_alloc();
                    hw_frame->format = AV_PIX_FMT_VAAPI;
                    hw_frame->width = width;
                    hw_frame->height = height;
                    hw_frame->hw_frames_ctx = av_buffer_ref(hw_frames);
                    if (av_hwframe_get_buffer(hw_frames, hw_frame, 0) < 0 ||
                        av_hwframe_transfer_data(hw_frame, picture, 0) < 0)
                        ok = false;
                    send_frame = hw_frame;
                }
                send_frame->pts = (int64_t)i;
                if (ok && avcodec_send_frame(context, send_frame) < 0) ok = false;
                if (ok) writePackets();
                if (hw_frame) av_frame_free(&hw_frame);
                av_frame_free(&picture);
            }
            if (ok)
            {
                avcodec_send_frame(context, nullptr);
                writePackets();
                av_write_trailer(format);
            }
            if (scaler) sws_freeContext(scaler);
            avio_closep(&format->pb);
            if (context->hw_frames_ctx) av_buffer_unref(&context->hw_frames_ctx);
            if (hw_frames) av_buffer_unref(&hw_frames);
            if (hw_device) av_buffer_unref(&hw_device);
            avcodec_free_context(&context);
            avformat_free_context(format);
            if (!ok)
            {
                if (error) *error = "H.264 encode failed (" + encoder_name + ")";
                return false;
            }
            stats_.backend = encoder_name;
            GEN_LOG_INFO("[Codec] encoded %zu frames with %s (in-process libavcodec)", frames.size(),
                         encoder_name.c_str());
            return true;
#endif
        }

        //------------------------------------------------------------------
        // Direct libavcodec / libavformat decode (any supported container)
        //------------------------------------------------------------------
        bool EncoderDecoderAPI::decodeH264(const std::string &path, std::vector<FrameImage> &frames,
                                            std::string *error)
        {
#if !GENESIS_ENABLE_FFMPEG
            (void)path; (void)frames;
            if (error) *error = "Genesis was built without FFmpeg";
            return false;
#else
            AVFormatContext *format = nullptr;
            int ret = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
            if (ret < 0)
            {
                if (error) *error = "cannot open media: " + avErrorString(ret);
                return false;
            }
            if (avformat_find_stream_info(format, nullptr) < 0)
            {
                if (error) *error = "cannot find streams";
                avformat_close_input(&format);
                return false;
            }
#if LIBAVFORMAT_VERSION_MAJOR >= 59
            const AVCodec *codec = nullptr;
#else
            AVCodec *codec = nullptr;
#endif
            const int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
            if (stream_index < 0 || !codec)
            {
                if (error) *error = "no video stream";
                avformat_close_input(&format);
                return false;
            }
            AVCodecContext *context = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(context, format->streams[stream_index]->codecpar);
            if (avcodec_open2(context, codec, nullptr) < 0)
            {
                if (error) *error = "cannot open decoder";
                avcodec_free_context(&context);
                avformat_close_input(&format);
                return false;
            }
            const int width = context->width;
            const int height = context->height;
            SwsContext *scaler = sws_getContext(width, height, context->pix_fmt, width, height, AV_PIX_FMT_RGBA,
                                                SWS_BILINEAR, nullptr, nullptr, nullptr);
            AVPacket *packet = av_packet_alloc();
            AVFrame *frame = av_frame_alloc();
            auto drain = [&]() -> bool
            {
                while (true)
                {
                    const int receive = avcodec_receive_frame(context, frame);
                    if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) return true;
                    if (receive < 0) return false;
                    FrameImage image;
                    image.width = (uint32_t)frame->width;
                    image.height = (uint32_t)frame->height;
                    image.rgba.resize((size_t)image.width * image.height * 4);
                    uint8_t *dst[4] = {image.rgba.data(), nullptr, nullptr, nullptr};
                    int dst_lines[4] = {(int)image.width * 4, 0, 0, 0};
                    sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, dst, dst_lines);
                    frames.push_back(std::move(image));
                    av_frame_unref(frame);
                }
            };
            bool ok = true;
            while (av_read_frame(format, packet) >= 0)
            {
                if (packet->stream_index == stream_index)
                {
                    if (avcodec_send_packet(context, packet) < 0) ok = false;
                    else if (!drain()) ok = false;
                }
                av_packet_unref(packet);
                if (!ok) break;
            }
            if (ok)
            {
                avcodec_send_packet(context, nullptr);
                ok = drain();
            }
            av_packet_free(&packet);
            av_frame_free(&frame);
            if (scaler) sws_freeContext(scaler);
            avcodec_free_context(&context);
            avformat_close_input(&format);
            if (!ok)
            {
                if (error) *error = "video decode failed";
                return false;
            }
            stats_.backend = std::string(codec->name) + " (libavcodec)";
            return !frames.empty();
#endif
        }

        std::unique_ptr<Media::MediaPlayer> EncoderDecoderAPI::createPlayer() const
        {
            return std::make_unique<Media::MediaPlayer>();
        }

    } // namespace API
} // namespace Genesis
