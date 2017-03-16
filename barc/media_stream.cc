//
//  media_stream.cc
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

extern "C" {
#include "media_stream.h"
#include <libavutil/audio_fifo.h>
#include <assert.h>
}

// Workaround C++ issue with ffmpeg macro
#ifndef __clang__
#undef av_err2str
#define av_err2str(errnum) \
av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
AV_ERROR_MAX_STRING_SIZE, errnum)
#endif



static int ensure_audio_frames(struct media_stream_s* stream);
static inline float pts_per_sample(float sample_rate, float num_samples,
                                   AVRational time_base);
static void insert_silence(struct media_stream_s* stream,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts);

struct media_stream_s {
  // media sources
  media_stream_get_frame_cb* audio_read_cb;
  void* audio_read_arg;
  media_stream_get_video_frame_cb* video_read_cb;
  void* video_read_arg;
  archive_get_config_cb* audio_config_cb;
  void* audio_config_arg;
  archive_get_config_cb* video_config_cb;
  void* video_config_arg;

  const char* sz_name;
  const char* sz_class;
  int source_width;
  int source_height;
  int x_offset;
  int y_offset;
  int z_index;
  int render_width;
  int render_height;
  enum object_fit object_fit;
};

#pragma mark - Media sources
void media_stream_set_video_read(struct media_stream_s* stream,
                                 media_stream_get_video_frame_cb* cb, void* p)
{
  stream->video_read_cb = cb;
  stream->video_read_arg = p;
}

void media_stream_set_audio_read(struct media_stream_s* stream,
                                   media_stream_get_frame_cb* cb, void* p)
{
  stream->audio_read_cb = cb;
  stream->audio_read_arg = p;
}

void media_stream_set_audio_config_callback
(struct media_stream_s* stream, archive_get_config_cb* cb, void* p)
{
  stream->audio_config_cb = cb;
  stream->audio_read_arg = p;
}

void media_stream_set_video_config_callback
(struct media_stream_s* stream, archive_get_config_cb* cb, void* p)
{
  stream->video_config_cb = cb;
  stream->video_config_arg = p;
}

# pragma mark - memory lifecycle

int media_stream_alloc(struct media_stream_s** stream_out)
{
  struct media_stream_s* stream =
  (struct media_stream_s*) calloc(1, sizeof(struct media_stream_s));
  *stream_out = stream;
  return 0;
}

int media_stream_free(struct media_stream_s* stream)
{
    free(stream);
    return 0;
}

#pragma mark - Video stream management

int archive_stream_get_video_for_time(struct media_stream_s* stream,
                                      struct smart_frame_t** smart_frame,
                                      double clock_time)
{
  int ret = stream->video_read_cb(stream, smart_frame, clock_time,
                                  stream->video_read_arg);
  if (ret) {
    printf("failed to get video frame for stream %s t=%f\n",
           stream->sz_name, clock_time);
  }
  return ret;
}

int archive_stream_get_audio_samples(struct media_stream_s* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out,
                                     int num_channels,
                                     double clock_time)
{
  assert(48000 == sample_rate);
  assert(format == AV_SAMPLE_FMT_S16);

  AVFrame* frame = av_frame_alloc();
  frame->channels = num_channels;
  frame->format = format;
  frame->sample_rate = sample_rate;
  frame->nb_samples = num_samples;
  frame->channel_layout = AV_CH_LAYOUT_MONO;
  int ret = av_frame_get_buffer(frame, 0);
  if (ret) {
    printf("unable to prepare audio buffer for reading: %s\n",
           av_err2str(ret));
    return ret;
  }
  ret = stream->audio_read_cb(stream, frame, clock_time,
                                  stream->audio_read_arg);
  if (!ret) {
    printf("failed to get video frame for stream %s t=%f",
           stream->sz_name, clock_time);
    return ret;
  }
  assert(frame->channels == num_channels);
  for (int i = 0; i < num_channels; i++) {
    memcpy(samples_out[i], frame->data[i],
           num_samples * av_get_bytes_per_sample(format));
  }
  av_frame_free(&frame);
  return ret;
}

static inline float pts_per_sample(float sample_rate, float num_samples,
                            AVRational time_base)
{
    // (duration * time_base) * (sample_rate) == sample_count
    // (20 / 1000) * 48000 == 960
    return
    num_samples * ((float)time_base.den / (float)time_base.num) / sample_rate;
}

#pragma mark - Getters & Setters

int archive_stream_get_offset_x(struct media_stream_s* stream) {
    return stream->x_offset;
}

int archive_stream_get_offset_y(struct media_stream_s* stream) {
    return stream->y_offset;
}

int archive_stream_get_render_width(struct media_stream_s* stream) {
    return stream->render_width;
}

int archive_stream_get_render_height(struct media_stream_s* stream) {
    return stream->render_height;
}

int archive_stream_get_z_index(const struct media_stream_s* stream) {
    return stream->z_index;
}

enum object_fit archive_stream_get_object_fit(struct media_stream_s* stream) {
    return stream->object_fit;
}

void archive_stream_set_offset_x(struct media_stream_s* stream,
                                 int x_offset)
{
    stream->x_offset = x_offset;
}

void archive_stream_set_offset_y(struct media_stream_s* stream,
                                 int y_offset)
{
    stream->y_offset = y_offset;
}

void archive_stream_set_render_width(struct media_stream_s* stream,
                                     int width)
{
    stream->render_width = width;
}

void archive_stream_set_render_height(struct media_stream_s* stream,
                                      int height)
{
    stream->render_height = height;
}

void archive_stream_set_z_index(struct media_stream_s* stream,
                                int z_index)
{
    stream->z_index = z_index;
}


void archive_stream_set_object_fit(struct media_stream_s* stream,
                                   enum object_fit object_fit)
{
    stream->object_fit = object_fit;
}

void media_stream_set_name(struct media_stream_s* stream,
                           const char* sz_name)
{
  stream->sz_name = sz_name;
}

void media_stream_set_class(struct media_stream_s* stream,
                            const char* sz_class)
{
  stream->sz_class = sz_class;
}

const char* media_stream_get_name(struct media_stream_s* stream) {
    return stream->sz_name;
}

const char* media_stream_get_class(struct media_stream_s* stream) {
    return stream->sz_class;
}
