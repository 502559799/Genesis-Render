# Encoder & Decoder API

`Genesis::API::EncoderDecoderAPI` is the cross-platform media facade.  H.264
encode/decode and audio/video playback are **linked directly into the engine**
through FFmpeg's libraries — no `ffmpeg` subprocess is launched at run time.

## 1. Dependencies

| Dependency | Purpose |
|---|---|
| `libavformat` | demuxing / muxing (MP4, MKV, ...) |
| `libavcodec` | H.264/AAC/... decode and encode |
| `libswscale` | RGBA8 ↔ YUV/NV12 conversion |
| `libswresample` | decoded audio → interleaved S16 48 kHz stereo |
| `libavutil` | frames, hardware devices, options |
| SDL2 | audio output device (engine already depends on SDL2) |
| zlib / stb_image | Raw/PNG frame codecs |

CMake option `GENESIS_ENABLE_FFMPEG` (default ON) detects the headers/libraries
via `../deps/prefix`, `GENESIS_FFMPEG_ROOT`, `/opt/homebrew` or `/usr/local`.
The source is guarded for the FFmpeg 4.x (legacy `channel_layout` /
`swr_alloc_set_opts` / `AVCodec::pix_fmts`) and FFmpeg 5.1+/7/8
(`AVChannelLayout` / `swr_alloc_set_opts2`, no `pix_fmts`) APIs.  If the
libraries are missing, Raw/PNG still work and H.264/playback reports
unavailable.

macOS / Homebrew:

```bash
brew install ffmpeg
cmake -S . -B build -DGENESIS_FFMPEG_ROOT=/opt/homebrew
```

`scripts/fetch_deps.sh` downloads the Ubuntu 22.04 `libav*-dev` packages into
`../deps/prefix`.

## 2. Codecs

| Codec | Implementation | Hardware |
|---|---|---|
| Raw | zlib-compressed RGBA `.gseq` sequence | no |
| PNG | PNG frames inside `.gseq` | no |
| H.264 | `libavcodec` encoder into MP4 via `libavformat` | NVENC / VA-API / VideoToolbox when a device exists; libx264 otherwise |

Capability probing uses `avcodec_find_encoder_by_name()` plus
`av_hwdevice_ctx_create()`:

- `h264_nvenc` → CUDA device;
- `h264_vaapi` → `/dev/dri/renderD128`;
- `h264_videotoolbox` → VideoToolbox device (macOS);
- `h264_qsv` is reported but not auto-selected (the hw-frame path is future work);
- `libx264` is the software fallback.

`codecs()` reports `hardware`, `available` and a detail string for each entry.
In this headless container only `libx264` is available; the hardware entries
are correctly reported as unavailable.

## 3. Encode / decode API

```cpp
API::EncoderDecoderAPI codec;
codec.initialize();

std::vector<API::FrameImage> frames = ...; // RGBA8
API::CodecOptions options;
options.codec = API::Codec::H264;
options.fps = 30;
options.prefer_hardware = true;

codec.encodeSequence(frames, "out.mp4", options);

std::vector<API::FrameImage> decoded;
codec.decodeSequence("out.mp4", decoded);
```

- H.264 encode: `avcodec_alloc_context3` → `avcodec_open2` →
  `avformat_write_header` → `avcodec_send_frame` / `avcodec_receive_packet` →
  `av_interleaved_write_frame`.
- H.264 decode: `avformat_open_input` → `avcodec_send_packet` /
  `avcodec_receive_frame` → `sws_scale` to RGBA8.
- Single-frame Raw/PNG stay in-process (`zlib`, engine PNG writer,
  `stb_image` decode).

## 4. Audio/video playback

`EncoderDecoderAPI::createPlayer()` returns a `Media::MediaPlayer` (also part
of the EncoderDecoder API domain):

```cpp
auto player = engine.encoderDecoder().createPlayer();
player->open("movie.mp4", /*enable_audio=*/true);
player->play();
// each frame:
player->update(dt);
const uint8_t *rgba = player->videoPixels();   // upload to an RHI texture
size_t samples = player->queuedAudioSamples(); // S16 stereo @ 48 kHz
```

- **Video**: libavcodec decodes H.264/HEVC/...; libswscale converts to RGBA8
  (row 0 = top).  A bounded frame queue keeps playback smooth.
- **Audio**: libavcodec decodes AAC/MP3/...; libswresample converts to
  interleaved S16 48 kHz stereo; an SDL2 audio device pulls from a ring buffer.
- **Transport**: play/pause, seek (av_seek_frame + decoder flush), volume,
  position/duration, codec/resolution/FPS metadata.
- **Headless**: `open(path, /*enable_audio=*/false)` still decodes and queues
  audio PCM for tests; the demo sets `SDL_AUDIODRIVER=dummy` in headless mode.

## 5. Demo

```bash
./build/bin/genesis_demo --scene media --lab media
```

- plays `assets/media/sample.mp4` (H.264 video + AAC audio, generated offline),
- video is uploaded to an RHI texture and drawn with the batch path,
- audio is played through SDL,
- controls: play/pause, seek, volume, reopen with/without audio device.

The Encoder / Decoder lab (`--lab codec`) additionally provides Raw/PNG/H.264
roundtrips, codec capability table and "encode current frame".

## 6. Tests

- `codec.raw_and_png_roundtrip` — exact lossless roundtrip.
- `codec.h264_hardware_or_software_roundtrip` — direct libavcodec H.264
  encode/decode; in this environment the selected encoder is `libx264`.
- `codec.media_player_decodes_video_and_audio` — opens
  `assets/media/sample.mp4`, checks H.264/AAC metadata, decoded video frames,
  queued audio PCM, seek and `decodeAllVideo()`.

## 7. Limitations

- Audio **encoding** is not implemented yet; the API decodes audio and encodes
  video.  Adding AAC encode is a small extension using the same libavcodec
  abstraction.
- Hardware encode currently covers NVENC, VA-API and VideoToolbox through
  FFmpeg's hardware encoders; QSV is reported but not selected.
- Real-time playback is file-based; network/streaming protocols are not
  exposed.
