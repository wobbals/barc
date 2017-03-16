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

  struct media_stream_s* media_stream;
};

int file_media_source_alloc(struct file_media_source_s** media_source_out)
{
  struct file_media_source_s* this = (struct file_media_source_s*)
  calloc(1, sizeof(struct file_media_source_s));
  media_stream_alloc(&this->media_stream);
  *media_source_out = this;
  return 0;
}

void file_media_source_free(struct file_media_source_s* this) {
  avcodec_close(this->video_context);
  avcodec_close(this->audio_context);
  avformat_close_input(&this->audio_format_context);
  avformat_close_input(&this->video_format_context);
  media_stream_free(this->media_stream);
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

// ensures contiguous frames available to the sample fifo
static int audio_frame_fifo_pop(struct media_stream_s* stream) {
  int ret = ensure_audio_frames(stream);
  if (ret) {
    return ret;
  }

  // release the previous head of the queue
  AVFrame* old_frame = stream->audio_frame_fifo.front();
  stream->audio_frame_fifo.pop_front();

  // once again, make sure there's more data available
  ret = ensure_audio_frames(stream);
  if (ret) {
    return ret;
  }
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);
  AVRational audio_time_base = {0};
  audio_time_base.num = 1;
  audio_time_base.den = stream_config.sample_rate;
  AVFrame* new_frame = stream->audio_frame_fifo.front();
  // insert silence if we detect a lapse in audio continuity
  if ((new_frame->pts - old_frame->pts) > old_frame->pkt_duration) {
    int64_t num_samples =
    samples_per_pts(stream_config.sample_rate,
                    new_frame->pts - old_frame->pts -
                    old_frame->pkt_duration,
                    audio_time_base);
    if (num_samples > 0) {
      printf("data gap detected at %lld. generate %lld silent samples\n",
             old_frame->pts, num_samples);
      insert_silence(stream, num_samples, old_frame->pts, new_frame->pts);
    }
  }

  av_frame_free(&old_frame);

  return ret;
}

// pops frames off the frame fifo and copies samples to sample fifo
static int get_more_audio_samples(struct media_stream_s* stream) {
  int ret = ensure_audio_frames(stream);
  AVFrame* frame = NULL;
  if (ret) {
    return ret;
  }
  frame = stream->audio_frame_fifo.front();
  // consume the frame completely
  if (frame) {
    stream->audio_last_pts = frame->pts;
    ret = av_audio_fifo_write(stream->audio_sample_fifo,
                              (void**)frame->data, frame->nb_samples);
    assert(ret == frame->nb_samples);
    //        printf("consume offset audio pts %lld %s\n",
    //               stream->start_offset + frame->pts, stream->sz_name);
  }
  ret = audio_frame_fifo_pop(stream);
  return ret;
}

static int read_audio_frame(struct media_stream_s* stream)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);

  /* pump packet reader until fifo is populated, or file ends */
  while (stream->audio_frame_fifo.empty()) {
    ret = av_read_frame(stream->audio_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = av_frame_alloc();
    got_frame = 0;
    ret = avcodec_decode_audio4(stream_config.context, frame,
                                &got_frame, &packet);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Error decoding audio: %s\n",
             av_err2str(ret));
    }

    if (got_frame) {
      frame->pts = av_frame_get_best_effort_timestamp(frame);
      stream->audio_frame_fifo.push_back(frame);
    }

    av_packet_unref(&packet);
  }

  return !got_frame;
}

// shortcut for frequent checks to the frame fifo
static int ensure_audio_frames(struct media_stream_s* stream) {
  int ret = 0;
  if (stream->audio_frame_fifo.empty()) {
    ret = read_audio_frame(stream);
  }
  return ret;
}

/* Editorial: Splitting the input reader into two separate instances allows
 * us to seek through the source file for specific data without altering
 * progress of other stream tracks. The alternative is to seek once and keep
 * extra data in a fifo. Sometimes this causes memory to run away, so instead
 * we just read the same input file twice.
 */
static int read_video_frame(struct file_media_source_s* pthis,
                            double clock_time)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };
  struct stream_config_s stream_config = {0};
  stream->video_config_cb(&stream_config, stream->video_config_arg);

  /* pump packet reader until fifo is populated, or file ends */
  while (stream->video_fifo.empty()) {
    ret = av_read_frame(stream->video_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = NULL;
    ret = stream->video_read_cb(stream, &frame, clock_time)
    got_frame = 0;
    ret = avcodec_decode_video2(stream_config.context, frame,
                                &got_frame, &packet);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Error decoding video: %s\n",
             av_err2str(ret));
    }

    if (got_frame) {
      frame->pts = av_frame_get_best_effort_timestamp(frame);
      smart_frame_t* smart_frame;
      smart_frame_create(&smart_frame, frame);
      stream->video_fifo.push(smart_frame);
    }

    av_packet_unref(&packet);
  }

  return !got_frame;
}

//for posterity -- delete this
void old_media_stream_audio_read() {

  int ret = 0;
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);
  int64_t local_ts = clock_time * stream_config.sample_rate;
  int requested_pts_interval = num_samples / stream_config.sample_rate;
  // TODO: This needs to be aware of the sample rate and format of the
  // receiver
  printf("pop %d time units of audio samples (%d total) for local ts %lld\n",
         requested_pts_interval, num_samples, local_ts);
  printf("audio fifo size before: %d\n",
         av_audio_fifo_size(stream->audio_sample_fifo));
  while (num_samples > av_audio_fifo_size(stream->audio_sample_fifo) && !ret)
  {
    ret = get_more_audio_samples(stream);
  }
  printf("audio fifo size after: %d\n",
         av_audio_fifo_size(stream->audio_sample_fifo));

  if (ret) {
    return ret;
  }

  ret = av_audio_fifo_read(stream->audio_sample_fifo,
                           (void**)samples_out, num_samples);
  assert(ret == num_samples);
  printf("pop %d audio samples for local ts %lld %s\n",
         num_samples, local_ts, stream->sz_name);
  int64_t offset_ts = stream->audio_last_pts + stream_config.start_offset;
  int clock_drift = (int) (local_ts - offset_ts);
  printf("stream %s audio clock drift=%d local_ts=%lld offset_ts=%lld\n",
         stream->sz_name, clock_drift, local_ts, offset_ts);
  if (clock_drift > 1000) {
    // global clock is ahead of the stream. truncate some data (better yet,
    // squish some samples together
  } else if (clock_drift < -1000) {
    // stream is ahead of the global clock. introduce some silence or
    // spread samples apart
  }

}

void file_media_get_frame_callback() {

  int ret = ensure_video_frame(stream);
  if (ret) {
    *smart_frame = NULL;
    return ret;
  }
  assert(!stream->video_fifo.empty());
  smart_frame_t* smart_front = stream->video_fifo.front();
  AVFrame* front = smart_frame_get(smart_front);
  struct stream_config_s stream_config = {0};
  stream->video_config_cb(&stream_config, stream->video_config_arg);
  AVRational video_time_base = {0};
  video_time_base.num = 1;
  video_time_base.den = stream_config.sample_rate;
  int64_t local_time = av_rescale_q(clock_time, clock_time_base,
                                    video_time_base);
  int64_t offset_pts = front->pts + stream_config.start_offset;

  // pop frames off the fifo until we're caught up
  while (offset_pts < local_time) {
    smart_frame_release(smart_front);
    stream->video_fifo.pop();
    if (ensure_video_frame(stream)) {
      return -1;
    }
    smart_front = stream->video_fifo.front();
    front = smart_frame_get(smart_front);
    offset_pts = front->pts + stream_config.start_offset;
  }

  // after a certain point, we'll stop duplicating frames.
  // TODO: make this configurable maybe?
  if (local_time - offset_pts > 3000) {
    *smart_frame = NULL;
  } else {
    *smart_frame = smart_front;
  }

  return (NULL == *smart_frame);

}

int file_media_source_open(struct file_media_source_s** source_out,
                           const char *filename,
                           double start_offset, double stop_offset,
                           const char* stream_name,
                           const char* stream_class)
{
  int ret = file_media_source_alloc(source_out);
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
                                  double clock_time)
{
  return (this->start_offset <= clock_time &&
          clock_time < this->stop_offset);
}

int64_t file_stream_get_stop_offset(struct file_media_source_s* this)
{
  return this->stop_offset;
}

int64_t file_stream_get_start_offset(struct file_media_source_s* this)
{
  return this->start_offset;
}

struct media_stream_s* file_media_source_get_stream
(struct file_media_source_s* source)
{
  return source->media_stream;
}
