//
//  barc.cc
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//

extern "C" {
#include "barc.h"
#include "file_writer.h"
#include "media_stream.h"
#include "video_mixer.h"
#include "audio_mixer.h"
}
#include <algorithm>
#include <vector>

// Workaround C++ issue with ffmpeg macro
#ifndef __clang__
#undef av_err2str
#define av_err2str(errnum) \
av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

struct barc_s {
  std::vector<struct media_stream_s*> streams;
  size_t out_width;
  size_t out_height;
  struct file_writer_t* file_writer;
  struct video_mixer_s* video_mixer;
  const char* output_path;

  char need_track[2];
  double global_clock;
  double next_clock_times[2];
  double audio_tick_time;
  double video_tick_time;
};

static int tick_audio(struct barc_s* barc);
static void compute_audio_times(struct barc_s* barc);

#pragma mark - Memory lifecycle

void barc_alloc(struct barc_s** barc_out) {
  struct barc_s* barc = (struct barc_s*)calloc(1, sizeof(struct barc_s));
  barc->streams = std::vector<struct media_stream_s*>();
  video_mixer_alloc(&barc->video_mixer);
  *barc_out = barc;
}

void barc_free(struct barc_s* barc) {
  for (struct media_stream_s* stream : barc->streams) {
    media_stream_free(stream);
  }
  video_mixer_free(barc->video_mixer);
  file_writer_free(barc->file_writer);
  free(barc);
}

int barc_read_configuration(struct barc_s* barc, struct barc_config_s* config)
{
  barc->video_tick_time = 1.0 / config->video_framerate;
  barc->output_path = config->output_path;
  barc->out_width = config->out_width;
  barc->out_height = config->out_height;
  video_mixer_set_width(barc->video_mixer, barc->out_width);
  video_mixer_set_height(barc->video_mixer, barc->out_height);
  video_mixer_set_css_preset(barc->video_mixer, config->css_preset);
  video_mixer_set_css_custom(barc->video_mixer, config->css_custom);
  return 0;
}

int barc_open_outfile(struct barc_s* barc) {
  file_writer_alloc(&barc->file_writer);
  int ret = file_writer_open(barc->file_writer,
                             barc->output_path,
                             (int) barc->out_width,
                             (int) barc->out_height);
  compute_audio_times(barc);
  return ret;
}

int barc_close_outfile(struct barc_s* barc) {
  printf("Waiting for video mixer to finish...");
  video_mixer_flush(barc->video_mixer);
  printf("..done!\n");

  printf("Close file writer..");
  int ret = file_writer_close(barc->file_writer);
  printf("..done!\n");

  return ret;
}

//add media source. this must be safely repeatable.
int barc_add_source(struct barc_s* barc, struct barc_source_s* source) {
  auto index = std::find(barc->streams.begin(), barc->streams.end(),
                         source->media_stream);
  if (index == barc->streams.end()) {
    barc->streams.push_back(source->media_stream);
  }
  return 0;
}

//remove stream this must be safely repeatable.
int barc_remove_source(struct barc_s* barc, struct barc_source_s* source) {
  auto index = std::find(barc->streams.begin(), barc->streams.end(),
                           source->media_stream);
  if (index != barc->streams.end()) {
    barc->streams.erase(index);
  }
  return 0;
}

int barc_tick(struct barc_s* barc) {
  printf("barc.tick: global_clock:%f need_audio:%d need_video:%d\n",
         barc->global_clock, barc->need_track[0], barc->need_track[1]);
  int aret = 0;
  int vret = 0;
  // process audio and video tracks, as needed
  if (barc->need_track[0]) {
    // TODO: create and delegate audio mixdown task to another class
    aret = tick_audio(barc);
    barc->next_clock_times[0] = barc->global_clock + barc->audio_tick_time;
  }

  if (barc->need_track[1]) {
    video_mixer_clear_streams(barc->video_mixer);
    for (struct media_stream_s* stream : barc->streams) {
      video_mixer_add_stream(barc->video_mixer, stream);
    }
    //TODO: get the millisecond time base from encoder format context
    vret = video_mixer_async_push_frame(barc->video_mixer,
                                        barc->file_writer,
                                        barc->global_clock,
                                        barc->global_clock * 1000
                                        );
    barc->next_clock_times[1] = barc->global_clock + barc->video_tick_time;
  }

  // calculate exactly when we need to wake up again.
  // first, assume audio is next.
  double next_clock = barc->next_clock_times[0];
  if (fabs(next_clock - barc->next_clock_times[1]) < 0.0001) {
    // if we land on a common factor of both track intervals, floating
    // point math might not make a perfect match between the two
    // floating timestamps. grab both tracks on the next tick
    barc->need_track[1] = 1;
    barc->need_track[0] = 1;
  } else if (next_clock > barc->next_clock_times[1]) {
    // otherwise, check to see if audio is indeed the next track
    next_clock = barc->next_clock_times[1];
    barc->need_track[1] = 1;
    barc->need_track[0] = 0;
  } else {
    // take only what we need
    barc->need_track[0] = 1;
    barc->need_track[1] = 0;
  }

  barc->global_clock = next_clock;

  return aret & vret;
}

double barc_get_current_clock(struct barc_s* barc) {
  return barc->global_clock;
}

static int tick_audio(struct barc_s* barc)
{
  int ret;

  // configure next audio frame to be encoded
  AVFrame* output_frame = av_frame_alloc();
  output_frame->format = barc->file_writer->audio_ctx_out->sample_fmt;
  output_frame->channel_layout =
  barc->file_writer->audio_ctx_out->channel_layout;
  output_frame->nb_samples =
  barc->file_writer->audio_ctx_out->frame_size;
  // output pts is offset back to zero for late starts (see -b option)
  output_frame->pts = barc->global_clock *
  barc->file_writer->audio_ctx_out->time_base.den;
  output_frame->sample_rate = barc->file_writer->audio_ctx_out->sample_rate;

  ret = av_frame_get_buffer(output_frame, 1);
  if (ret) {
    printf("No output AVFrame buffer to write audio. Error: %s\n",
           av_err2str(ret));
    return ret;
  }
  // clear the buffers of garbage just in case things get weird
  for (int i = 0; i < output_frame->channels; i++) {
    memset(output_frame->data[i], 0,
           output_frame->nb_samples *
           av_get_bytes_per_sample
           ((enum AVSampleFormat)output_frame->format));
  }

  // convert to c-style array
  size_t stream_count = barc->streams.size();
  struct media_stream_s** active_streams = (struct media_stream_s**)
  calloc(stream_count, sizeof(struct media_stream_s*));
  for (int i = 0; i < stream_count; i++) {
    active_streams[i] = barc->streams[i];
  }

  // mix down samples using original time
  audio_mixer_get_samples_for_streams
  (active_streams, stream_count, barc->global_clock, output_frame);
  free(active_streams);

  // send it to the audio filter graph
  file_writer_push_audio_frame(barc->file_writer, output_frame);
  return ret;
}

static void compute_audio_times(struct barc_s* barc) {
  double out_frame_size = barc->file_writer->audio_ctx_out->frame_size;
  double out_sample_rate = barc->file_writer->audio_ctx_out->sample_rate;
  barc->audio_tick_time = out_frame_size / out_sample_rate;

}
