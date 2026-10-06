//The decoders under the PSP's music and movies (kernel.hpp's AudioDecoder and VideoDecoder): FFmpeg's LGPL ones, in
//builds that have them (ARES_ENABLE_FFMPEG: the shared libraries libavcodec and libavutil, built from FFmpeg's
//release by thirdparty/ffmpeg/build.sh with nothing but these decoders). The libraries hand them whole frames and
//whole access units, having taken the PSP's containers apart themselves, so none of FFmpeg's parsers or demuxers are
//needed, and they convert the samples and pixels themselves. Without FFmpeg the system makes no decoders, and the
//libraries refuse their streams.

#if defined(ARES_ENABLE_FFMPEG)

namespace {

//A frame's bytes, followed by the zeros FFmpeg's bit readers may read into past its end.
auto padded(std::vector<u8>& room, const u8* data, u32 size) -> u8* {
  room.assign(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
  if(size) memcpy(room.data(), data, size);
  return room.data();
}

struct FFmpegAudio : AudioDecoder {
  AVCodecContext* context = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  u32 channels = 0;
  std::vector<u8> room;

  ~FFmpegAudio() {
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
  }

  auto open(const Format& format) -> bool {
    AVCodecID id = AV_CODEC_ID_NONE;
    if(format.codec == Codec::Atrac3) id = AV_CODEC_ID_ATRAC3;
    if(format.codec == Codec::Atrac3plus) id = AV_CODEC_ID_ATRAC3P;
    if(format.codec == Codec::Mp3) id = AV_CODEC_ID_MP3;
    auto codec = avcodec_find_decoder(id);
    if(!codec || !(context = avcodec_alloc_context3(codec))) return false;
    channels = format.channels;
    av_channel_layout_default(&context->ch_layout, format.channels);
    context->sample_rate = format.rate;
    context->block_align = format.frameBytes;
    context->thread_count = 1;
    if(!format.extra.empty()) {
      context->extradata = (u8*)av_mallocz(format.extra.size() + AV_INPUT_BUFFER_PADDING_SIZE);
      if(!context->extradata) return false;
      memcpy(context->extradata, format.extra.data(), format.extra.size());
      context->extradata_size = format.extra.size();
    }
    if(avcodec_open2(context, codec, nullptr) < 0) return false;
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    return packet && frame;
  }

  //Each frame FFmpeg gives back (its samples as floats or 16-bit numbers, a plane a channel or interleaved), into
  //16-bit samples interleaved, the floats scaled by 32768 and rounded to the nearest, clamped; channels it lacks
  //repeat its last.
  auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 override {
    packet->data = padded(this->room, data, size);
    packet->size = size;
    if(avcodec_send_packet(context, packet) < 0) return -1;
    u32 made = 0;
    while(avcodec_receive_frame(context, frame) >= 0) {
      u32 have = frame->ch_layout.nb_channels, count = std::min<u32>(frame->nb_samples, room - made);
      auto format = AVSampleFormat(frame->format);
      bool planar = av_sample_fmt_is_planar(format);
      for(u32 n = 0; n < count; n++) {
        for(u32 c = 0; c < channels; c++) {
          u32 from = std::min(c, have - 1);
          u32 index = planar ? n : n * have + from;
          const u8* plane = frame->extended_data[planar ? from : 0];
          s32 value = 0;
          if(format == AV_SAMPLE_FMT_FLT || format == AV_SAMPLE_FMT_FLTP) {
            value = std::lrint(((const float*)plane)[index] * 32768.0f);
          } else if(format == AV_SAMPLE_FMT_S16 || format == AV_SAMPLE_FMT_S16P) {
            value = ((const s16*)plane)[index];
          } else if(format == AV_SAMPLE_FMT_S32 || format == AV_SAMPLE_FMT_S32P) {
            value = ((const s32*)plane)[index] >> 16;
          }
          samples[(made + n) * channels + c] = std::clamp(value, -32768, 32767);
        }
      }
      made += count;
      av_frame_unref(frame);
    }
    return made;
  }

  auto reset() -> void override {
    avcodec_flush_buffers(context);
  }
};

struct FFmpegVideo : VideoDecoder {
  AVCodecContext* context = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* shown = nullptr;
  Picture shownPicture;
  std::vector<u8> room;

  ~FFmpegVideo() {
    av_frame_free(&shown);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
  }

  //One thread, so each access unit's picture comes as the PSP's decoder gives it, with no frames held back for
  //threads to work on.
  auto open() -> bool {
    auto codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if(!codec || !(context = avcodec_alloc_context3(codec))) return false;
    context->thread_count = 1;
    context->max_pixels = s64(MaxSide) * MaxSide;
    if(avcodec_open2(context, codec, nullptr) < 0) return false;
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    shown = av_frame_alloc();
    return packet && frame && shown;
  }

  //The last picture out of the decoder, in 8-bit 4:2:0 (what the PSP's movies are; any other kind, or one larger
  //than MaxSide either way, is passed over).
  auto decode(const u8* data, u32 size) -> bool override {
    packet->data = padded(room, data, size);
    packet->size = size;
    if(avcodec_send_packet(context, packet) < 0) return false;
    bool came = false;
    while(avcodec_receive_frame(context, frame) >= 0) {
      if((frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) || frame->width <= 0
         || frame->height <= 0 || u32(frame->width) > MaxSide || u32(frame->height) > MaxSide) {
        av_frame_unref(frame);
        continue;
      }
      av_frame_unref(shown);
      av_frame_move_ref(shown, frame);
      came = true;
    }
    if(came) {
      shownPicture.width = shown->width;
      shownPicture.height = shown->height;
      for(u32 n = 0; n < 3; n++) {
        shownPicture.planes[n] = shown->data[n];
        shownPicture.strides[n] = shown->linesize[n];
      }
    }
    return came;
  }

  auto picture() const -> const Picture& override {
    return shownPicture;
  }

  auto reset() -> void override {
    avcodec_flush_buffers(context);
    av_frame_unref(shown);
    shownPicture = {};
  }
};

auto ffmpegAudio(const AudioDecoder::Format& format) -> std::unique_ptr<AudioDecoder> {
  auto decoder = std::make_unique<FFmpegAudio>();
  if(!decoder->open(format)) return {};
  return decoder;
}

auto ffmpegVideo() -> std::unique_ptr<VideoDecoder> {
  auto decoder = std::make_unique<FFmpegVideo>();
  if(!decoder->open()) return {};
  return decoder;
}

}

#endif

//The decoders the system makes: FFmpeg's, where the build has them (its own messages kept quiet: a frame that won't
//decode is the libraries' to report).
static auto codecDecoders(Kernel& kernel) -> void {
#if defined(ARES_ENABLE_FFMPEG)
  av_log_set_level(AV_LOG_QUIET);
  kernel.audioDecoders = ffmpegAudio;
  kernel.videoDecoders = ffmpegVideo;
#else
  (void)kernel;
#endif
}

//The calling thread waits for the Media Engine's decoding (atrac.cpp, mp3.cpp, mpeg.cpp), where it may, other
//threads running meanwhile, as they do on a PSP; the call returns its result (already set) when the wait ends.
auto Kernel::codecWait(u32 microseconds) -> void {
  if(!current || interrupting || dispatchSuspended || !interruptsEnabled) return;
  current->waitCount = cpu.ipu.r[2];
  block(Wait::Codec, 0, cycles + u64(microseconds) * (CPUFrequency / 1'000'000));
}
