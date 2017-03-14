//
//  file_media_source.c
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#include "file_media_source.h"

// we know this to be true from documentation, it's not discoverable :-(
static const AVRational archive_manifest_timebase = { 1, 1000 };

struct file_media_source_s {
  const char* filename;
  // file source attributes
  int64_t start_offset;
  int64_t stop_offset;
  int64_t duration;

  // read the same file twice to split the read functions
  AVFormatContext* video_format_context;
  AVFormatContext* audio_format_context;
  // decode contexts
  AVCodecContext* video_context;
  AVCodecContext* audio_context;
  // indices for dereferencing contexts
  int video_stream_index;
  int audio_stream_index;
};

int file_media_source_alloc(struct file_media_source_s** media_source_out)
{
  struct file_media_source_s* this = (struct file_media_source_s*)
  calloc(1, sizeof(struct file_media_source_s));
  *media_source_out = this;
  return 0;
}

void file_media_source_free(struct file_media_source_s* this) {
  avcodec_close(this->video_context);
  avcodec_close(this->audio_context);
  avformat_close_input(&this->audio_format_context);
  avformat_close_input(&this->video_format_context);
  free(this);
}

#pragma mark - Container setup

static int archive_open_codec(AVFormatContext* format_context,
                              enum AVMediaType media_type,
                              AVCodecContext** codec_context,
                              int* stream_index)
{
  int ret;
  AVCodec *dec;

  /* select the video stream */
  ret = av_find_best_stream(format_context, media_type,
                            -1, -1, &dec, 0);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR,
           "Cannot find a video stream in the input file\n");
    return ret;
  }
  // prefer libopus over built-in opus
  if (AV_CODEC_ID_OPUS == dec->id && strcmp("libopus", dec->name)) {
    printf("Switch from %s to libopus\n", dec->name);
    dec = avcodec_find_decoder_by_name("libopus");
  }
  *stream_index = ret;
  *codec_context = format_context->streams[*stream_index]->codec;

  av_opt_set_int(*codec_context, "refcounted_frames", 1, 0);

  /* init the decoder */
  ret = avcodec_open2(*codec_context, dec, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
    return ret;
  }

  return 0;
}

int archive_stream_open_file(struct archive_stream_t** stream_out,
                             const char *filename,
                             int64_t start_offset, int64_t stop_offset,
                             const char* stream_name,
                             const char* stream_class)
{
  int ret = archive_stream_alloc(stream_out, stream_name, stream_class);
  if (ret) {
    printf("Could not allocate new stream");
    return ret;
  }
  struct file_media_source_s* this;
  file_media_source_alloc(&this);
  this->start_offset = start_offset;
  this->stop_offset = stop_offset;
  this->filename = filename;

  ret = avformat_open_input(&this->audio_format_context, filename,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  ret = avformat_open_input(&this->video_format_context, filename,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  // does this actually work? i don't think so.
  this->audio_context->request_sample_fmt = AV_SAMPLE_FMT_S16;

  archive_open_codec(this->video_format_context,
                     AVMEDIA_TYPE_VIDEO,
                     &this->video_context,
                     &this->video_stream_index);
  archive_open_codec(this->audio_format_context,
                     AVMEDIA_TYPE_AUDIO,
                     &this->audio_context,
                     &this->audio_stream_index);

  assert(this->audio_context->sample_fmt = AV_SAMPLE_FMT_S16);

  // TODO: Bring this back if possible -- it's still a good check
//  AVStream* audio_stream =
//  stream->audio_format_context->streams[this->audio_stream_index];
//  ret = ensure_audio_frames(this);
//  if (ret) {
//    // don't worry about audio fifos. we'll silently fail in case there is
//    // still meaningful data inside the conatiner.
//  }

  // if there is good audio data later on, but it doesn't arrive at the
  // start offset time,
  // add silence to head of queue before the first audio packet plays out
//  if (!ret && stream->audio_frame_fifo.front()->pts > 0) {
//    AVFrame* frame = stream->audio_frame_fifo.front();
//    int64_t num_samples =
//    samples_per_pts(stream->audio_context->sample_rate,
//                    frame->pts,
//                    audio_stream->time_base);
//    if (num_samples > 0) {
//      // Silence should run from time 0 to the first real packet pts
//      insert_silence(stream, num_samples, 0, frame->pts);
//    }
//  }

  return 0;
}

int file_stream_is_active_at_time(struct file_media_source_s* this,
                                  int64_t global_time,
                                  AVRational time_base)
{

  int64_t local_time = av_rescale_q(global_time, time_base,
                                    archive_manifest_timebase);
  return (this->start_offset <= local_time &&
          local_time < this->stop_offset);
}

int64_t file_stream_get_stop_offset(struct file_media_source_s* this)
{
  return this->stop_offset;
}

int64_t file_stream_get_start_offset(struct file_media_source_s* this)
{
  return this->start_offset;
}
