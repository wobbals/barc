//
//  file_audio_source.c
//  barc
//
//  Created by Charley Robinson on 3/23/17.
//

extern "C" {
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>

#include "file_audio_source.h"
}

#include <deque>

// Workaround C++ issue with ffmpeg macro
#ifndef __clang__
#undef av_err2str
#define av_err2str(errnum) \
av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

static int open_file_stream(struct file_audio_source_s* pthis);
static int pump(struct file_audio_source_s* pthis);

struct file_audio_source_s {
  AVFormatContext* format_context;
  AVCodecContext* codec_context;
  AVCodec* codec;
  int stream_index;

  std::deque<AVFrame*> audio_frame_fifo;
  AVAudioFifo* audio_sample_fifo;
  double sample_head_time;

  const char* file_path;
};

void file_audio_source_alloc(struct file_audio_source_s** source_out) {
  struct file_audio_source_s* pthis = (struct file_audio_source_s*)
  calloc(1, sizeof(struct file_audio_source_s));
  pthis->audio_frame_fifo = std::deque<AVFrame*>();
  // should this be dynamically sized?
  pthis->audio_sample_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 4098);
  pthis->sample_head_time = 0.;
  *source_out = pthis;
}

void file_audio_source_free(struct file_audio_source_s* pthis) {
  while (!pthis->audio_frame_fifo.empty()) {
    AVFrame* frame = pthis->audio_frame_fifo.front();
    pthis->audio_frame_fifo.pop_front();
    av_frame_free(&frame);
  }
  av_audio_fifo_free(pthis->audio_sample_fifo);

  avcodec_close(pthis->codec_context);
  avformat_close_input(&pthis->format_context);
  free(pthis);
}

int file_audio_source_load_config(struct file_audio_source_s* pthis,
                                  struct file_audio_config_s* config)
{
  pthis->file_path = config->file_path;
  open_file_stream(pthis);
  return 0;
}

int file_audio_source_seek(struct file_audio_source_s* pthis, double to_time)
{
  // stream timebase or codec time base?
  AVRational format_time_base =
  pthis->format_context->streams[pthis->stream_index]->time_base;

  int ret;
  // avformat_seek methods don't work, so instead start the stream from the top
  // and pop frames off the stream until we're caught up
  ret = avformat_flush(pthis->format_context);
  ret = av_seek_frame(pthis->format_context, pthis->stream_index, 0, 0);
  av_audio_fifo_reset(pthis->audio_sample_fifo);
  pthis->sample_head_time = 0;
  AVPacket pkt = { 0 };
  // this is a destructive loop: don't call this method if you need to read
  // every packet from the file!
  while (!ret && pthis->sample_head_time < to_time) {
    av_read_frame(pthis->format_context, &pkt);
    pthis->sample_head_time = (double)pkt.pts / format_time_base.den;
    av_packet_unref(&pkt);
  }
  if (ret) {
    return ret;
  }

  return ret;
}

double file_audio_source_get_pos(struct file_audio_source_s* pthis) {
  return pthis->sample_head_time;
}

int file_audio_source_get_next(struct file_audio_source_s* pthis,
                               int num_samples, int16_t* samples_out)
{
  int ret = 0;
  while (!ret && av_audio_fifo_size(pthis->audio_sample_fifo) < num_samples)
  {
    ret = pump(pthis);
  }
  ret = av_audio_fifo_read(pthis->audio_sample_fifo,
                           (void**)&samples_out, num_samples);
  pthis->sample_head_time +=
  ((double)num_samples / pthis->codec_context->time_base.den);
  return ret;
}

#pragma mark - Internal utilities

static int open_file_stream(struct file_audio_source_s* pthis)
{
  int ret;
  ret = avformat_open_input(&pthis->format_context, pthis->file_path,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  /* select appropriate stream */
  ret = av_find_best_stream(pthis->format_context,
                            AVMEDIA_TYPE_AUDIO,
                            -1, -1,
                            &pthis->codec, 0);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR,
           "Cannot find a video stream in the input file\n");
    return ret;
  }
  // prefer libopus over built-in opus
  if (AV_CODEC_ID_OPUS == pthis->codec->id &&
      strcmp("libopus", pthis->codec->name))
  {
    printf("Switch from %s to libopus\n", pthis->codec->name);
    pthis->codec = avcodec_find_decoder_by_name("libopus");
  }
  pthis->stream_index = ret;
  pthis->codec_context =
  pthis->format_context->streams[pthis->stream_index]->codec;

  assert(pthis->codec_context->sample_fmt = AV_SAMPLE_FMT_S16);
  pthis->codec_context->request_sample_fmt = AV_SAMPLE_FMT_S16;

  av_opt_set_int(pthis->codec_context, "refcounted_frames", 1, 0);

  /* init the decoder */
  ret = avcodec_open2(pthis->codec_context, pthis->codec, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
  }

  return ret;
}

static inline int64_t samples_per_pts(int sample_rate, int64_t pts,
                                      AVRational time_base)
{
  // (duration * time_base) * (sample_rate) == sample_count
  // (20 / 1000) * 48000 == 960
  return (float)((float)pts * (float)time_base.num) /
  (float)time_base.den * (float)sample_rate;
  //return av_rescale_q(pts, time_base, { sample_rate, 1});
}

static AVFrame* generate_silence(struct file_audio_source_s* pthis,
                                 double duration)
{
  AVFrame* silence = av_frame_alloc();
  silence->sample_rate = pthis->codec_context->sample_rate;
  silence->nb_samples = duration * silence->sample_rate;
  silence->format = pthis->codec_context->sample_fmt;
  silence->channel_layout = pthis->codec_context->channel_layout;
  av_frame_get_buffer(silence, 1);
  for (int i = 0; i < silence->channels; i++) {
    memset(silence->data[i], 0,
           silence->nb_samples *
           av_get_bytes_per_sample((enum AVSampleFormat)silence->format));
  }

  return silence;
}

static int read_audio_frame(struct file_audio_source_s* pthis)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };

  /* pump packet reader until fifo is populated, or file ends */
  while (pthis->audio_frame_fifo.empty()) {
    ret = av_read_frame(pthis->format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = av_frame_alloc();
    got_frame = 0;
    if (packet.stream_index == pthis->stream_index) {
      ret = avcodec_decode_audio4(pthis->codec_context, frame,
                                  &got_frame, &packet);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error decoding audio: %s\n",
               av_err2str(ret));
      }

      if (got_frame) {
        frame->pts = av_frame_get_best_effort_timestamp(frame);
        pthis->audio_frame_fifo.push_back(frame);
      }
    } else {
      av_frame_free(&frame);
    }

    av_packet_unref(&packet);
  }

  return !got_frame;
}

// shortcut for frequent checks to the frame fifo
static int ensure_audio_frames(struct file_audio_source_s* stream) {
  int ret = 0;
  if (stream->audio_frame_fifo.empty()) {
    ret = read_audio_frame(stream);
  }
  return ret;
}

// ensures contiguous frames available to the sample fifo
static int audio_frame_fifo_pop(struct file_audio_source_s* pthis) {
  int ret = ensure_audio_frames(pthis);
  if (ret) {
    return ret;
  }

  // release the previous head of the queue
  AVFrame* old_frame = pthis->audio_frame_fifo.front();
  pthis->audio_frame_fifo.pop_front();

  // once again, make sure there's more data available
  ret = ensure_audio_frames(pthis);
  if (ret) {
    return ret;
  }

  av_frame_free(&old_frame);

  return ret;
}

static void check_frame_sync(struct file_audio_source_s* pthis,
                             AVFrame* frame)
{
  //convert frame pts, check it against the current head time
  double sample_fifo_duration =
  (double)av_audio_fifo_size(pthis->audio_sample_fifo) /
  pthis->codec_context->time_base.den;
  AVStream* this_stream = pthis->format_context->streams[pthis->stream_index];
  double frame_time = (double)frame->pts / this_stream->time_base.den;
#define FRAME_SYNC_THRESHOLD 0.25 // what value shoud this be?
  // if there's a difference between the time at the end of the sample fifo
  // and the PTS of this frame, insert silence to the sample fifo equal to
  // that difference. this mostly is expected to happen if we are parked over
  // a time where there is no audio in the file stream
  if (frame_time - (pthis->sample_head_time + sample_fifo_duration) >
      FRAME_SYNC_THRESHOLD)
  {
    double silence_needed = frame_time - (pthis->sample_head_time +
                                          sample_fifo_duration);
    AVFrame* silence = generate_silence(pthis, silence_needed);
    av_audio_fifo_write(pthis->audio_sample_fifo,
                        (void**)silence->data, silence->nb_samples);
    av_frame_unref(silence);
  }
}

static int pump(struct file_audio_source_s* pthis) {
  int ret = ensure_audio_frames(pthis);
  AVFrame* frame = NULL;
  if (ret) {
    return ret;
  }
  frame = pthis->audio_frame_fifo.front();
  // consume the frame completely
  if (frame) {
    // make sure frame media belongs at the head of the queue
    check_frame_sync(pthis, frame);
    ret = av_audio_fifo_write(pthis->audio_sample_fifo,
                              (void**)frame->data, frame->nb_samples);
    assert(ret == frame->nb_samples);
    //        printf("consume offset audio pts %lld %s\n",
    //               stream->start_offset + frame->pts, stream->sz_name);
  }
  ret = audio_frame_fifo_pop(pthis);
  return ret;
}
