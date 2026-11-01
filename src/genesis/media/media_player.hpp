/*
 * Genesis Render - in-process audio/video player
 *
 * Demuxes and decodes with libavformat/libavcodec, converts video to RGBA8
 * (libswscale) and audio to interleaved S16 48 kHz stereo (libswresample),
 * and plays audio through an SDL2 audio device.  No external process is
 * spawned; this is the playback half of the EncoderDecoder API.
 */
#ifndef __GENESIS_MEDIA_PLAYER_HPP__
#define __GENESIS_MEDIA_PLAYER_HPP__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace Media
    {
        struct MediaInfo
        {
            double duration = 0.0;
            int width = 0;
            int height = 0;
            double fps = 0.0;
            std::string video_codec;
            std::string audio_codec;
            int sample_rate = 0;
            int channels = 0;
            bool has_video = false;
            bool has_audio = false;
        };

        class MediaPlayer
        {
        public:
            MediaPlayer();
            ~MediaPlayer();

            bool open(const std::string &path, bool enable_audio = true, std::string *error = nullptr);
            void close();
            bool isOpen() const;

            bool update(double delta_seconds);
            void play();
            void pause();
            void toggle();
            bool playing() const;
            bool seek(double seconds);

            double position() const;
            double duration() const;
            const MediaInfo &info() const;

            bool hasVideo() const;
            bool hasAudio() const;
            int videoWidth() const;
            int videoHeight() const;
            // RGBA8, row 0 = top.  `serial` changes whenever a new frame is
            // presented, so callers can avoid re-uploading unchanged frames.
            const uint8_t *videoPixels() const;
            uint64_t videoSerial() const;

            // Number of decoded S16 stereo samples currently queued (also
            // filled when the audio device is disabled, for tests).
            size_t queuedAudioSamples() const;
            void setVolume(float volume); // 0..1
            float volume() const;
            bool audioDeviceOpen() const;

            // Decodes all remaining video frames (used by tools / tests).
            bool decodeAllVideo(std::vector<std::vector<uint8_t>> &frames, int &width, int &height);

        private:
            struct Impl;
            static void audioCallback(void *userdata, uint8_t *stream, int len);
            std::unique_ptr<Impl> impl_;
        };
    } // namespace Media
} // namespace Genesis

#endif /* __GENESIS_MEDIA_PLAYER_HPP__ */
