/*
 * Genesis Render - in-process audio/video player implementation
 */
#include "media/media_player.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "foundation_layer/math/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>

#ifndef GENESIS_ENABLE_FFMPEG
#define GENESIS_ENABLE_FFMPEG 0
#endif

#if GENESIS_ENABLE_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <SDL2/SDL.h>
#endif

namespace Genesis
{
    namespace Media
    {
#if GENESIS_ENABLE_FFMPEG
        namespace
        {
            constexpr int kOutSampleRate = 48000;
            constexpr int kOutChannels = 2;
            constexpr size_t kMaxQueuedSamples = (size_t)kOutSampleRate * kOutChannels * 4; // 4 s

            std::string avErrorString(int code)
            {
                char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(code, buffer, sizeof(buffer));
                return std::string(buffer);
            }
        } // namespace

        struct MediaPlayer::Impl
        {
            struct VideoFrame
            {
                double pts = 0.0;
                std::vector<uint8_t> rgba;
            };

            AVFormatContext *format = nullptr;
            AVCodecContext *video_decoder = nullptr;
            AVCodecContext *audio_decoder = nullptr;
            int video_stream = -1;
            int audio_stream = -1;
            double video_time_base = 1.0 / 1000.0;
            SwsContext *scaler = nullptr;
            SwrContext *resampler = nullptr;
            MediaInfo info;
            bool open = false;
            bool playing = false;
            bool eof = false;
            bool flushed = false;
            double clock = 0.0;

            std::deque<VideoFrame> video_queue;
            std::vector<uint8_t> current;
            uint64_t serial = 0;

            mutable std::mutex audio_mutex;
            std::vector<int16_t> audio_ring;
            size_t audio_read = 0;
            size_t audio_write = 0;
            size_t audio_count = 0;
            bool audio_device_enabled = false;
            SDL_AudioDeviceID audio_device = 0;
            float volume = 1.0f;

            bool convertVideo(AVFrame *frame, std::vector<uint8_t> &out)
            {
                if (!frame) return false;
                const int w = frame->width > 0 ? frame->width : info.width;
                const int h = frame->height > 0 ? frame->height : info.height;
                if (w <= 0 || h <= 0) return false;
                if (!scaler)
                {
                    scaler = sws_getContext(w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGBA,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!scaler) return false;
                }
                out.resize((size_t)w * h * 4);
                uint8_t *dst[4] = {nullptr, nullptr, nullptr, nullptr};
                int dst_lines[4] = {0, 0, 0, 0};
                if (av_image_fill_arrays(dst, dst_lines, out.data(), AV_PIX_FMT_RGBA, w, h, 1) < 0) return false;
                sws_scale(scaler, frame->data, frame->linesize, 0, h, dst, dst_lines);
                info.width = w;
                info.height = h;
                return true;
            }

            void pushAudio(const uint8_t *data, int size)
            {
                if (!data || size <= 0) return;
                const int16_t *samples = reinterpret_cast<const int16_t *>(data);
                const size_t count = (size_t)size / sizeof(int16_t);
                std::lock_guard<std::mutex> lock(audio_mutex);
                if (audio_ring.empty()) audio_ring.resize(kMaxQueuedSamples);
                for (size_t i = 0; i < count; ++i)
                {
                    if (audio_count >= kMaxQueuedSamples) break;
                    audio_ring[audio_write] = samples[i];
                    audio_write = (audio_write + 1) % kMaxQueuedSamples;
                    ++audio_count;
                }
            }

            bool decodeVideoPacket(AVPacket *packet)
            {
                if (!video_decoder) return false;
                if (avcodec_send_packet(video_decoder, packet) < 0 && packet) return false;
                AVFrame *frame = av_frame_alloc();
                bool any = false;
                while (frame)
                {
                    const int ret = avcodec_receive_frame(video_decoder, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;
                    VideoFrame vf;
                    const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                            ? frame->best_effort_timestamp
                                            : (int64_t)(clock / std::max(1e-9, video_time_base));
                    vf.pts = (double)pts * video_time_base;
                    if (convertVideo(frame, vf.rgba)) { video_queue.push_back(std::move(vf)); any = true; }
                    av_frame_unref(frame);
                }
                av_frame_free(&frame);
                return any;
            }

            bool decodeAudioPacket(AVPacket *packet)
            {
                if (!audio_decoder) return false;
                if (avcodec_send_packet(audio_decoder, packet) < 0 && packet) return false;
                AVFrame *frame = av_frame_alloc();
                bool any = false;
                while (frame)
                {
                    const int ret = avcodec_receive_frame(audio_decoder, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;
                    if (!resampler)
                    {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
                        // FFmpeg 5.1+ / 7+ / 8+: AVChannelLayout + opts2.
                        AVChannelLayout in_layout;
                        if (frame->ch_layout.nb_channels > 0)
                            av_channel_layout_copy(&in_layout, &frame->ch_layout);
                        else
                            av_channel_layout_default(&in_layout, 2);
                        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
                        const int alloc_ret = swr_alloc_set_opts2(
                            &resampler, &out_layout, AV_SAMPLE_FMT_S16, kOutSampleRate, &in_layout,
                            (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
                        av_channel_layout_uninit(&in_layout);
                        if (alloc_ret < 0 || (resampler && swr_init(resampler) < 0))
                        {
                            swr_free(&resampler);
                        }
#else
                        // FFmpeg 4.x: legacy channel_layout / channels API.
                        uint64_t in_layout = frame->channel_layout;
                        if (!in_layout) in_layout = av_get_default_channel_layout(frame->channels);
                        resampler = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                                       in_layout, (AVSampleFormat)frame->format, frame->sample_rate, 0,
                                                       nullptr);
                        if (resampler && swr_init(resampler) < 0)
                        {
                            swr_free(&resampler);
                        }
#endif
                        info.sample_rate = kOutSampleRate;
                        info.channels = kOutChannels;
                    }
                    if (resampler)
                    {
                        const int out_samples = (int)av_rescale_rnd(
                            swr_get_delay(resampler, frame->sample_rate) + frame->nb_samples, kOutSampleRate,
                            frame->sample_rate, AV_ROUND_UP);
                        std::vector<int16_t> buffer((size_t)out_samples * kOutChannels);
                        uint8_t *out[1] = {reinterpret_cast<uint8_t *>(buffer.data())};
                        const int got = swr_convert(resampler, out, out_samples,
                                                   (const uint8_t **)frame->data, frame->nb_samples);
                        if (got > 0)
                        {
                            pushAudio(reinterpret_cast<const uint8_t *>(buffer.data()), got * kOutChannels * 2);
                            any = true;
                        }
                    }
                    av_frame_unref(frame);
                }
                av_frame_free(&frame);
                return any;
            }

            void readPackets(int max_packets)
            {
                if (eof) return;
                int packets = 0;
                AVPacket *packet = av_packet_alloc();
                while (packet && packets < max_packets)
                {
                    if (video_queue.size() >= 8) break; // keep the display queue bounded
                    if (audio_count > (size_t)kOutSampleRate * kOutChannels) break; // keep ~1 s buffered
                    const int ret = av_read_frame(format, packet);
                    if (ret < 0)
                    {
                        eof = true;
                        if (!flushed)
                        {
                            flushed = true;
                            if (video_decoder) decodeVideoPacket(nullptr);
                            if (audio_decoder) decodeAudioPacket(nullptr);
                        }
                        break;
                    }
                    ++packets;
                    if (packet->stream_index == video_stream) decodeVideoPacket(packet);
                    else if (packet->stream_index == audio_stream)
                    {
                        if (audio_count > (size_t)kOutSampleRate * kOutChannels)
                        {
                            av_packet_unref(packet);
                            continue; // audio buffer is full; keep decoding video
                        }
                        decodeAudioPacket(packet);
                    }
                    av_packet_unref(packet);
                }
                av_packet_free(&packet);
            }

            void present()
            {
                while (!video_queue.empty() && video_queue.front().pts <= clock)
                {
                    current = std::move(video_queue.front().rgba);
                    video_queue.pop_front();
                    ++serial;
                }
                if (current.empty() && !video_queue.empty())
                {
                    current = std::move(video_queue.front().rgba);
                    video_queue.pop_front();
                    ++serial;
                }
            }

            void clearQueues()
            {
                video_queue.clear();
                current.clear();
                std::lock_guard<std::mutex> lock(audio_mutex);
                audio_read = audio_write = audio_count = 0;
            }
        };

        void MediaPlayer::audioCallback(void *userdata, uint8_t *stream, int len)
        {
            Impl *impl = static_cast<Impl *>(userdata);
            int16_t *out = reinterpret_cast<int16_t *>(stream);
            const size_t samples = (size_t)len / sizeof(int16_t);
            if (!impl)
            {
                std::memset(stream, 0, (size_t)len);
                return;
            }
            std::lock_guard<std::mutex> lock(impl->audio_mutex);
            for (size_t i = 0; i < samples; ++i)
            {
                if (impl->audio_count == 0)
                {
                    out[i] = 0;
                    continue;
                }
                float value = impl->audio_ring[impl->audio_read] * impl->volume;
                value = std::max(-32768.0f, std::min(32767.0f, value));
                out[i] = (int16_t)value;
                impl->audio_read = (impl->audio_read + 1) % kMaxQueuedSamples;
                --impl->audio_count;
            }
        }

        MediaPlayer::MediaPlayer() : impl_(std::make_unique<Impl>()) {}
        MediaPlayer::~MediaPlayer() { close(); }

        bool MediaPlayer::open(const std::string &path, bool enable_audio, std::string *error)
        {
            close();
            Impl &im = *impl_;
            int ret = avformat_open_input(&im.format, path.c_str(), nullptr, nullptr);
            if (ret < 0)
            {
                if (error) *error = "cannot open media: " + avErrorString(ret);
                close();
                return false;
            }
            ret = avformat_find_stream_info(im.format, nullptr);
            if (ret < 0)
            {
                if (error) *error = "cannot read stream info: " + avErrorString(ret);
                close();
                return false;
            }
            im.info.duration = im.format->duration > 0 ? (double)im.format->duration / AV_TIME_BASE : 0.0;

#if LIBAVFORMAT_VERSION_MAJOR >= 59
            const AVCodec *video_codec = nullptr;
#else
            AVCodec *video_codec = nullptr;
#endif
            im.video_stream = av_find_best_stream(im.format, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
            if (im.video_stream >= 0 && video_codec)
            {
                im.video_decoder = avcodec_alloc_context3(video_codec);
                avcodec_parameters_to_context(im.video_decoder, im.format->streams[im.video_stream]->codecpar);
                im.video_decoder->thread_count = 0;
                if (avcodec_open2(im.video_decoder, video_codec, nullptr) < 0)
                {
                    if (error) *error = "cannot open video decoder";
                    close();
                    return false;
                }
                im.video_time_base = av_q2d(im.format->streams[im.video_stream]->time_base);
                im.info.has_video = true;
                im.info.video_codec = video_codec->name;
                im.info.width = im.video_decoder->width;
                im.info.height = im.video_decoder->height;
                AVRational rate = av_guess_frame_rate(im.format, im.format->streams[im.video_stream], nullptr);
                im.info.fps = rate.den ? av_q2d(rate) : 0.0;
            }

#if LIBAVFORMAT_VERSION_MAJOR >= 59
            const AVCodec *audio_codec = nullptr;
#else
            AVCodec *audio_codec = nullptr;
#endif
            im.audio_stream = av_find_best_stream(im.format, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
            if (im.audio_stream >= 0 && audio_codec)
            {
                im.audio_decoder = avcodec_alloc_context3(audio_codec);
                avcodec_parameters_to_context(im.audio_decoder, im.format->streams[im.audio_stream]->codecpar);
                if (avcodec_open2(im.audio_decoder, audio_codec, nullptr) < 0)
                {
                    if (error) *error = "cannot open audio decoder";
                    close();
                    return false;
                }
                im.info.has_audio = true;
                im.info.audio_codec = audio_codec->name;
                im.info.sample_rate = im.audio_decoder->sample_rate;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
                im.info.channels = im.audio_decoder->ch_layout.nb_channels;
#else
                im.info.channels = im.audio_decoder->channels;
#endif
            }

            if (enable_audio && im.info.has_audio)
            {
                if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
                {
                    SDL_AudioSpec want{};
                    want.freq = kOutSampleRate;
                    want.format = AUDIO_S16SYS;
                    want.channels = kOutChannels;
                    want.samples = 1024;
                    want.callback = &MediaPlayer::audioCallback;
                    want.userdata = &im;
                    SDL_AudioSpec have{};
                    im.audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
                    if (im.audio_device != 0)
                    {
                        im.audio_device_enabled = true;
                        SDL_PauseAudioDevice(im.audio_device, 1);
                        GEN_LOG_INFO("[Media] SDL audio device open: %d Hz, %d ch", have.freq, have.channels);
                    }
                    else
                    {
                        GEN_LOG_WARN("[Media] SDL audio device unavailable: %s (video still plays)", SDL_GetError());
                    }
                }
            }
            im.open = true;
            im.playing = false;
            im.clock = 0.0;
            // Pre-buffer a couple of frames and some audio.
            im.readPackets(256);
            im.present();
            if (im.audio_device_enabled) SDL_PauseAudioDevice(im.audio_device, 1);
            GEN_LOG_INFO("[Media] opened '%s': %s %dx%d @ %.2f fps, audio %s %d Hz, duration %.2f s",
                         path.c_str(), im.info.video_codec.c_str(), im.info.width, im.info.height, im.info.fps,
                         im.info.audio_codec.c_str(), im.info.sample_rate, im.info.duration);
            return true;
        }

        void MediaPlayer::close()
        {
            if (!impl_) return;
            Impl &im = *impl_;
            if (im.audio_device) SDL_CloseAudioDevice(im.audio_device);
            im.audio_device = 0;
            im.audio_device_enabled = false;
            if (im.scaler) sws_freeContext(im.scaler);
            if (im.resampler) swr_free(&im.resampler);
            if (im.video_decoder) avcodec_free_context(&im.video_decoder);
            if (im.audio_decoder) avcodec_free_context(&im.audio_decoder);
            if (im.format) avformat_close_input(&im.format);
            im.scaler = nullptr;
            im.video_stream = im.audio_stream = -1;
            im.video_queue.clear();
            im.current.clear();
            im.audio_ring.clear();
            im.audio_read = im.audio_write = im.audio_count = 0;
            im.open = false;
            im.playing = false;
            im.eof = false;
            im.flushed = false;
            im.clock = 0.0;
        }

        bool MediaPlayer::isOpen() const { return impl_ && impl_->open; }

        bool MediaPlayer::update(double delta_seconds)
        {
            if (!impl_ || !impl_->open) return false;
            Impl &im = *impl_;
            if (im.playing)
            {
                im.clock += std::max(0.0, delta_seconds);
                if (im.info.duration > 0.0 && im.clock >= im.info.duration)
                {
                    im.clock = im.info.duration;
                    im.playing = false;
                    if (im.audio_device_enabled) SDL_PauseAudioDevice(im.audio_device, 1);
                }
            }
            im.readPackets(256);
            im.present();
            return true;
        }

        void MediaPlayer::play()
        {
            if (!impl_ || !impl_->open) return;
            impl_->playing = true;
            if (impl_->audio_device_enabled) SDL_PauseAudioDevice(impl_->audio_device, 0);
        }

        void MediaPlayer::pause()
        {
            if (!impl_) return;
            impl_->playing = false;
            if (impl_->audio_device_enabled) SDL_PauseAudioDevice(impl_->audio_device, 1);
        }

        void MediaPlayer::toggle() { if (impl_ && impl_->playing) pause(); else play(); }
        bool MediaPlayer::playing() const { return impl_ && impl_->playing; }
        double MediaPlayer::position() const { return impl_ ? impl_->clock : 0.0; }
        double MediaPlayer::duration() const { return impl_ ? impl_->info.duration : 0.0; }
        const MediaInfo &MediaPlayer::info() const { static MediaInfo empty; return impl_ ? impl_->info : empty; }
        bool MediaPlayer::hasVideo() const { return impl_ && impl_->info.has_video; }
        bool MediaPlayer::hasAudio() const { return impl_ && impl_->info.has_audio; }
        int MediaPlayer::videoWidth() const { return impl_ ? impl_->info.width : 0; }
        int MediaPlayer::videoHeight() const { return impl_ ? impl_->info.height : 0; }
        const uint8_t *MediaPlayer::videoPixels() const { return impl_ && !impl_->current.empty() ? impl_->current.data() : nullptr; }
        uint64_t MediaPlayer::videoSerial() const { return impl_ ? impl_->serial : 0; }

        size_t MediaPlayer::queuedAudioSamples() const
        {
            if (!impl_) return 0;
            std::lock_guard<std::mutex> lock(impl_->audio_mutex);
            return impl_->audio_count;
        }

        void MediaPlayer::setVolume(float volume) { if (impl_) impl_->volume = Math::saturate(volume); }
        float MediaPlayer::volume() const { return impl_ ? impl_->volume : 1.0f; }
        bool MediaPlayer::audioDeviceOpen() const { return impl_ && impl_->audio_device_enabled; }

        bool MediaPlayer::seek(double seconds)
        {
            if (!impl_ || !impl_->open) return false;
            Impl &im = *impl_;
            const int64_t ts = (int64_t)(std::max(0.0, seconds) * AV_TIME_BASE);
            if (av_seek_frame(im.format, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
            if (im.video_decoder) avcodec_flush_buffers(im.video_decoder);
            if (im.audio_decoder) avcodec_flush_buffers(im.audio_decoder);
            im.clearQueues();
            im.clock = std::max(0.0, seconds);
            im.eof = false;
            im.flushed = false;
            im.readPackets(256);
            im.present();
            return true;
        }

        bool MediaPlayer::decodeAllVideo(std::vector<std::vector<uint8_t>> &frames, int &width, int &height)
        {
            frames.clear();
            if (!impl_ || !impl_->open || impl_->video_stream < 0) return false;
            Impl &im = *impl_;
            av_seek_frame(im.format, -1, 0, AVSEEK_FLAG_BACKWARD);
            if (im.video_decoder) avcodec_flush_buffers(im.video_decoder);
            if (im.audio_decoder) avcodec_flush_buffers(im.audio_decoder);
            im.video_queue.clear();
            im.current.clear();
            AVPacket *packet = av_packet_alloc();
            while (packet && av_read_frame(im.format, packet) >= 0)
            {
                if (packet->stream_index == im.video_stream)
                {
                    const size_t before = im.video_queue.size();
                    im.decodeVideoPacket(packet);
                    for (size_t i = before; i < im.video_queue.size(); ++i)
                        frames.push_back(std::move(im.video_queue[i].rgba));
                    im.video_queue.clear();
                }
                av_packet_unref(packet);
            }
            if (packet)
            {
                im.decodeVideoPacket(nullptr);
                for (const auto &vf : im.video_queue) frames.push_back(vf.rgba);
                im.video_queue.clear();
            }
            av_packet_free(&packet);
            width = im.info.width;
            height = im.info.height;
            return !frames.empty();
        }
#else
        struct MediaPlayer::Impl {};
        MediaPlayer::MediaPlayer() : impl_(std::make_unique<Impl>()) {}
        MediaPlayer::~MediaPlayer() = default;
        bool MediaPlayer::open(const std::string &, bool, std::string *error)
        {
            if (error) *error = "Genesis was built without FFmpeg";
            return false;
        }
        void MediaPlayer::close() {}
        bool MediaPlayer::isOpen() const { return false; }
        bool MediaPlayer::update(double) { return false; }
        void MediaPlayer::play() {}
        void MediaPlayer::pause() {}
        void MediaPlayer::toggle() {}
        bool MediaPlayer::playing() const { return false; }
        bool MediaPlayer::seek(double) { return false; }
        double MediaPlayer::position() const { return 0.0; }
        double MediaPlayer::duration() const { return 0.0; }
        const MediaInfo &MediaPlayer::info() const { static MediaInfo m; return m; }
        bool MediaPlayer::hasVideo() const { return false; }
        bool MediaPlayer::hasAudio() const { return false; }
        int MediaPlayer::videoWidth() const { return 0; }
        int MediaPlayer::videoHeight() const { return 0; }
        const uint8_t *MediaPlayer::videoPixels() const { return nullptr; }
        uint64_t MediaPlayer::videoSerial() const { return 0; }
        size_t MediaPlayer::queuedAudioSamples() const { return 0; }
        void MediaPlayer::setVolume(float) {}
        float MediaPlayer::volume() const { return 1.0f; }
        bool MediaPlayer::audioDeviceOpen() const { return false; }
        bool MediaPlayer::decodeAllVideo(std::vector<std::vector<uint8_t>> &, int &, int &) { return false; }
#endif

    } // namespace Media
} // namespace Genesis
