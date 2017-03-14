//
//  barc.cc
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

extern "C" {
#include "barc.h"
#include "file_writer.h"
#include "archive_stream.h"
}

#include <vector>

#include "Geometry.h"

struct barc_s {
  std::vector<struct archive_stream_t*> streams;
  ArchiveLayout* layout;
  char auto_layout;
  int out_width;
  int out_height;
  struct file_writer_t* file_writer;
  struct frame_builder_t* frame_builder;
  char need_track[2];
  double global_clock;
  double next_clock_times[2];
  double audio_tick_time;
};

void open_container(struct barc_s* pthis) {

}

static void do_auto_layout(struct barc_s* pthis,
                    std::vector<ArchiveStreamInfo>& stream_info)
{
  char has_focus = 0;
  int non_focus_count = 0;
  for (auto info : stream_info) {
    if (0 == strcmp(info.layout_class().c_str(), "focus")) {
      has_focus = 1;
    } else {
      non_focus_count++;
    }
  }

  if (has_focus && non_focus_count < 2) {
    pthis->layout->setStyleSheet(Layout::kPip);
  } else if (has_focus && non_focus_count > 1) {
    pthis->layout->setStyleSheet(Layout::kHorizontalPresentation);
  } else {
    pthis->layout->setStyleSheet(Layout::kBestfitCss);
  }
}

static int archive_populate_stream_coords(struct barc_s* pthis,
                                   int64_t clock_time,
                                   AVRational clock_time_base)
{
  // Regenerate stream list every tick to allow on-the-fly layout changes
  std::vector<ArchiveStreamInfo> stream_info;
  for (struct archive_stream_t* stream : pthis->streams) {
    char will_use_stream =
    (archive_stream_is_active_at_time(stream, clock_time,
                                      clock_time_base) &&
     archive_stream_has_video_for_time(stream, clock_time,
                                       clock_time_base));
    if (will_use_stream)
    {
      stream_info.push_back
      (ArchiveStreamInfo(archive_stream_get_name(stream),
                         archive_stream_get_class(stream),
                         true));
    }
  }

  // switch between bestfit and horizontal presentation if in auto layout mode
  if (pthis->auto_layout) {
    do_auto_layout(pthis, stream_info);
  }

  StreamPositions positions = pthis->layout->layout(stream_info);
  for (ComposerLayoutStreamPosition position : positions) {
    for (struct archive_stream_t* stream : pthis->streams) {
      if (!strcmp(position.stream_id.c_str(),
                  archive_stream_get_name(stream))) {
        archive_stream_set_offset_x(stream, position.x);
        archive_stream_set_offset_y(stream, position.y);
        archive_stream_set_render_width(stream, position.width);
        archive_stream_set_render_height(stream, position.height);
        archive_stream_set_object_fit(stream,
                                      (enum object_fit)position.fit);
        archive_stream_set_z_index(stream, position.z);
      }
    }
  }
  return 0;
}

static int tick_audio(struct file_writer_t* file_writer,
                      struct archive_t* archive, int64_t clock_time,
                      AVRational clock_time_base,
                      int64_t clock_begin_offset, char skip_frame)
{
  int ret;

  // configure next audio frame to be encoded
  AVFrame* output_frame = av_frame_alloc();
  output_frame->format = file_writer->audio_ctx_out->sample_fmt;
  output_frame->channel_layout = file_writer->audio_ctx_out->channel_layout;
  output_frame->nb_samples = file_writer->audio_ctx_out->frame_size;
  // output pts is offset back to zero for late starts (see -b option)
  output_frame->pts = av_rescale_q(clock_time - clock_begin_offset,
                                   clock_time_base,
                                   file_writer->audio_ctx_out->time_base);
  output_frame->sample_rate = file_writer->audio_ctx_out->sample_rate;
  // adjust to local time in audio scale units, without begin offset
  int64_t local_source_ts =
  av_rescale_q(clock_time,
               clock_time_base,
               file_writer->audio_ctx_out->time_base);

  ret = av_frame_get_buffer(output_frame, 1);
  if (ret) {
    printf("No output AVFrame buffer to write audio. Error: %s\n",
           av_err2str(ret));
    return ret;
  }
  for (int i = 0; i < output_frame->channels; i++) {
    memset(output_frame->data[i], 0,
           output_frame->nb_samples *
           av_get_bytes_per_sample
           ((enum AVSampleFormat)output_frame->format));
  }


  // mix down samples using original time
  audio_mixer_get_samples_for_streams(active_streams, active_stream_count,
                                      local_source_ts,
                                      file_writer->audio_ctx_out->time_base,
                                      output_frame);

  if (!skip_frame) {
    // send it to the audio filter graph
    file_writer_push_audio_frame(file_writer, output_frame);
  }
  return ret;
}

struct frame_builder_callback_data_t {
  int64_t clock_time;
  int64_t clock_begin_offset;
  struct file_writer_t* file_writer;
};

static void frame_builder_cb(AVFrame* frame, void *p) {
  struct frame_builder_callback_data_t* data =
  ((struct frame_builder_callback_data_t*)p);
  int64_t clock_time = data->clock_time;
  int64_t clock_begin_offset = data->clock_begin_offset;
  struct file_writer_t* file_writer = data->file_writer;

  frame->pts = clock_time - clock_begin_offset;
  int ret = file_writer_push_video_frame(file_writer, frame);
  if (ret) {
    printf("Unable to push video frame %lld\n", frame->pts);
  }
  free(p);
}

static int tick_video(struct file_writer_t* file_writer,
                      struct frame_builder_t* frame_builder,
                      struct archive_t* archive, int64_t clock_time,
                      AVRational clock_time_base,
                      int64_t clock_begin_offset, char skip_frame)
{
  int ret = -1;
  if (skip_frame) {
    return 0;
  }

  archive_populate_stream_coords(archive, clock_time, clock_time_base);

  struct archive_stream_t** active_streams;
  int active_stream_count;

  archive_get_active_streams_for_time(archive, clock_time, clock_time_base,
                                      &active_streams,
                                      &active_stream_count);

  struct frame_builder_callback_data_t* callback_data =
  (struct frame_builder_callback_data_t*)
  malloc(sizeof(struct frame_builder_callback_data_t));
  callback_data->clock_time = clock_time;
  callback_data->clock_begin_offset = clock_begin_offset;
  callback_data->file_writer = file_writer;
  frame_builder_begin_frame(frame_builder,
                            file_writer->out_width,
                            file_writer->out_height,
                            (enum AVPixelFormat)AV_PIX_FMT_YUV420P,
                            callback_data);


  // append source frames to magic frame
  for (int i = 0; i < active_stream_count; i++) {
    struct archive_stream_t* stream = active_streams[i];
    if (!archive_stream_has_video_for_time(stream, clock_time,
                                           clock_time_base))
    {
      continue;
    }

    struct smart_frame_t* smart_frame;
    ret = archive_stream_get_video_for_time(stream, &smart_frame,
                                            clock_time, clock_time_base);
    if (NULL == smart_frame || ret) {
      continue;
    }

    struct frame_builder_subframe_t subframe;
    subframe.smart_frame = smart_frame;
    subframe.x_offset = archive_stream_get_offset_x(stream);
    subframe.y_offset = archive_stream_get_offset_y(stream);
    subframe.render_width = archive_stream_get_render_width(stream);
    subframe.render_height = archive_stream_get_render_height(stream);
    // TODO: configurable from the manifest
    subframe.object_fit = archive_stream_get_object_fit(stream);

    if (!skip_frame) {
      frame_builder_add_subframe(frame_builder, &subframe);
    }
  }
  if (!skip_frame) {
    frame_builder_finish_frame(frame_builder, frame_builder_cb);
  }
  return ret;
}


void tick(struct barc_s* this) {
  printf("need_audio:%d need_video:%d\n",
         this->need_track[0], this->need_track[1]);

  // process audio and video tracks, as needed
  if (this->need_track[0]) {
    tick_audio(file_writer, archive, this->global_clock, global_time_base,
               begin_offset, skip_frame);
    next_clock_times[0] = this->global_clock + audio_tick_time;
  }

  if (need_track[1]) {
    tick_video(this->file_writer, frame_builder, archive,
               global_clock, global_time_base,
               begin_offset, skip_frame);
    next_clock_times[1] = global_clock + video_tick_time;
  }

  // calculate exactly when we need to wake up again.
  // first, assume audio is next.
  double next_clock = next_clock_times[0];
  if (fabs(next_clock - next_clock_times[1]) < 0.0001) {
    // if we land on a common factor of both track intervals, floating
    // point math might not make a perfect match between the two
    // floating timestamps. grab both tracks on the next tick
    this->need_track[1] = 1;
    this->need_track[0] = 1;
  } else if (next_clock > this->next_clock_times[1]) {
    // otherwise, check to see if audio is indeed the next track
    this->next_clock = this->next_clock_times[1];
    this->need_track[1] = 1;
    this->need_track[0] = 0;
  } else {
    // take only what we need
    this->need_track[0] = 1;
    this->need_track[1] = 0;
  }

  this->global_clock = next_clock;

}
