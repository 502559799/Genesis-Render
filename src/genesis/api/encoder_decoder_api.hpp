/*
 * Genesis Render - Encoder & Decoder API
 *
 * A cross-platform media encode / decode facade:
 *
 *   - Raw / PNG frame codecs implemented in-process (zlib / stb_image) and
 *     usable on every platform without external tools.
 *   - H.264 sequence encode/decode through ffmpeg, with hardware encoder
 *     selection (NVENC / VA-API / QSV / VideoToolbox) when the platform and
 *     driver are actually present, and libx264 as the software fallback.
 *
 * `codecs()` reports what was detected; `lastBackend()` reports what the last
 * operation actually used.  The API is independent of the graphics backend.
 */
#ifndef __GENESIS_ENCODER_DECODER_API_HPP__
#define __GENESIS_ENCODER_DECODER_API_HPP__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace Media
    {
        class MediaPlayer;
    }

    namespace API
    {
        struct FrameImage
        {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<uint8_t> rgba; // width * height * 4

            bool valid() const { return width > 0 && height > 0 && rgba.size() == (size_t)width * height * 4; }
        };

        enum class Codec
        {
            Raw = 0, // zlib-compressed RGBA sequence (.gseq)
            PNG = 1, // lossless PNG frames (.gseq with PNG payloads)
            H264 = 2 // H.264 via libavcodec (hardware when available)
        };

        enum class HardwarePreference
        {
            Auto = 0,
            Disabled,
            NVENC,
            VAAPI,
            QSV,
            VideoToolbox
        };

        struct CodecOptions
        {
            Codec codec = Codec::H264;
            int quality = 23;
            int fps = 30;
            bool prefer_hardware = true;
            HardwarePreference hardware = HardwarePreference::Auto;
        };

        struct CodecInfo
        {
            std::string name;
            bool hardware = false;
            bool available = false;
            std::string detail;
        };

        struct CodecStats
        {
            double encode_ms = 0.0;
            double decode_ms = 0.0;
            uint32_t frames = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            std::string backend; // "raw", "png", "h264_nvenc", "libx264", ...
            std::string error;
        };

        class EncoderDecoderAPI
        {
        public:
            EncoderDecoderAPI() = default;
            ~EncoderDecoderAPI() = default;

            bool initialize();
            bool available() const { return ffmpeg_available_; }
            const std::vector<CodecInfo> &codecs() const { return codecs_; }
            const CodecStats &stats() const { return stats_; }
            const std::string &lastBackend() const { return stats_.backend; }

            bool encodeSequence(const std::vector<FrameImage> &frames, const std::string &path,
                                const CodecOptions &options = {}, std::string *error = nullptr);
            bool decodeSequence(const std::string &path, std::vector<FrameImage> &frames,
                                std::string *error = nullptr);

            bool encodeFrame(const FrameImage &frame, std::vector<uint8_t> &out, Codec codec = Codec::Raw,
                             std::string *error = nullptr);
            bool decodeFrame(const uint8_t *data, size_t size, FrameImage &out, std::string *error = nullptr);

            // Creates an audio/video player backed by the same libav* stack.
            // The player owns its demuxer, video/audio decoders and the SDL
            // audio output (optional, for headless tests pass enable_audio=false
            // to open()).
            std::unique_ptr<Media::MediaPlayer> createPlayer() const;

        private:
            std::string chooseH264Encoder(const CodecOptions &options) const;
            bool encodeH264(const std::vector<FrameImage> &frames, const std::string &path,
                            const CodecOptions &options, std::string *error);
            bool decodeH264(const std::string &path, std::vector<FrameImage> &frames, std::string *error);
            void probe();

            bool ffmpeg_available_ = false;
            std::vector<CodecInfo> codecs_;
            bool has_dri_ = false;
            CodecStats stats_;
        };
    } // namespace API
} // namespace Genesis

#endif /* __GENESIS_ENCODER_DECODER_API_HPP__ */
