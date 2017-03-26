//
//  file_media_source.c
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//

extern "C" {
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include "file_audio_source.h"
#include "file_media_source.h"
}

#include <deque>
#include <queue>

// Workaround C++ issue with ffmpeg macro
#ifndef __clang__
#undef av_err2str
#define av_err2str(errnum) \
av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

// we know this to be true from documentation, it's not discoverable :-(
static const AVRational archive_manifest_timebase = { 1, 1000 };

static void setup_media_stream(struct file_media_source_s* pthis);
int video_read_callback(struct media_stream_s* stream,
                        struct smart_frame_t** frame_out,
                        double time_clock,
                        void* p);
int audio_read_callback(struct media_stream_s* stream,
                        AVFrame* frame, double clock_time,
                        void* p);
  
struct file_media_source_s {
  const char* filename;
  // file source attributes
  double start_offset;
  double stop_offset;
  int64_t duration;

  // read the same file twice to split the read functions
  AVFormatContext* video_format_context;
  // decode contexts
  AVCodecContext* video_context;
  // indices for dereferencing contexts
  int video_stream_index;
  double video_ts_offset;
  struct file_audio_source_s* audio_source;

  std::queue<struct smart_frame_t*> video_fifo;

  struct media_stream_s* media_stream;
};

int file_media_source_alloc(struct file_media_source_s** media_source_out)
{
  struct file_media_source_s* pthis = (struct file_media_source_s*)
  calloc(1, sizeof(struct file_media_source_s));
  media_stream_alloc(&pthis->media_stream);
  pthis->video_fifo = std::queue<struct smart_frame_t*>();
  file_audio_source_alloc(&pthis->audio_source);
  *media_source_out = pthis;
  return 0;
}

void file_media_source_free(struct file_media_source_s* pthis) {
  while (!pthis->video_fifo.empty()) {
    smart_frame_release(pthis->video_fifo.front());
    pthis->video_fifo.pop();
  }
  avcodec_close(pthis->video_context);
  avformat_close_input(&pthis->video_format_context);
  media_stream_free(pthis->media_stream);
  file_audio_source_free(pthis->audio_source);
  free(pthis);
}

int file_media_source_seek(struct file_media_source_s* pthis,
                           double to_time)
{
  // seek audio source
  file_audio_source_seek(pthis->audio_source, to_time);
  // seek video source
  // TODO: video processing should be in a separate class
  pthis->video_ts_offset = to_time;
  return 0;
}

#pragma mark - Container setup

static int archive_open_codec(AVFormatContext* format_context,
                              enum AVMediaType media_type,
                              AVCodecContext** codec_context,
                              int* stream_index)
{
  int ret;
  AVCodec *dec;

  /* select appropriate stream */
  ret = av_find_best_stream(format_context, media_type,
                            -1, -1, &dec, 0);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR,
           "Cannot find a video stream in the input file\n");
    return ret;
  }
  *stream_index = ret;
  *codec_context = format_context->streams[*stream_index]->codec;

  av_opt_set_int(*codec_context, "refcounted_frames", 1, 0);

  /* init the decoder */
  ret = avcodec_open2(*codec_context, dec, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
  }

  return 0;
}

#pragma mark - Internal utilities

static inline int64_t samples_per_pts(int sample_rate, int64_t pts,
                                      AVRational time_base)
{
  // (duration * time_base) * (sample_rate) == sample_count
  // (20 / 1000) * 48000 == 960
  return (float)((float)pts * (float)time_base.num) /
  (float)time_base.den * (float)sample_rate;
  //return av_rescale_q(pts, time_base, { sample_rate, 1});
}

/* Editorial: Splitting the input reader into two separate instances allows
 * us to seek through the source file for specific data without altering
 * progress of other stream tracks. The alternative is to seek once and keep
 * extra data in a fifo. Sometimes this causes memory to run away, so instead
 * we just read the same input file twice.
 */
static int read_video_frame(struct file_media_source_s* pthis)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };

  /* pump packet reader until fifo is populated, or file ends */
  while (pthis->video_fifo.empty()) {
    ret = av_read_frame(pthis->video_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = av_frame_alloc();
    got_frame = 0;
    if (packet.stream_index == pthis->video_stream_index) {
      ret = avcodec_decode_video2(pthis->video_context, frame,
                                  &got_frame, &packet);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error decoding video: %s\n",
               av_err2str(ret));
      }

      if (got_frame) {
        frame->pts = av_frame_get_best_effort_timestamp(frame);
        smart_frame_t* smart_frame;
        smart_frame_create(&smart_frame, frame);
        pthis->video_fifo.push(smart_frame);
      }
    } else {
      av_frame_free(&frame);
    }

    av_packet_unref(&packet);
  }

  return !got_frame;
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
  struct file_media_source_s* pthis = *source_out;
  pthis->start_offset = start_offset;
  pthis->stop_offset = stop_offset;
  pthis->filename = filename;

  struct file_audio_config_s audio_config;
  audio_config.file_path = filename;
  ret = file_audio_source_load_config(pthis->audio_source, &audio_config);
  if (ret) {
    printf("unable to open audio source for reading\n");
    // what to do here?
    return ret;
  }

  ret = avformat_open_input(&pthis->video_format_context, filename,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  archive_open_codec(pthis->video_format_context,
                     AVMEDIA_TYPE_VIDEO,
                     &pthis->video_context,
                     &pthis->video_stream_index);

  media_stream_set_name(pthis->media_stream, stream_name);
  media_stream_set_class(pthis->media_stream, stream_class);
  setup_media_stream(pthis);

  return 0;
}

int file_stream_is_active_at_time(struct file_media_source_s* pthis,
                                  double clock_time)
{
  return (pthis->start_offset <= clock_time &&
          clock_time < pthis->stop_offset);
}

double file_stream_get_stop_offset(struct file_media_source_s* pthis)
{
  // don't forget to update this when seek method is called and video has it's
  // own class!
  return pthis->stop_offset - pthis->video_ts_offset;
}

struct media_stream_s* file_media_source_get_stream
(struct file_media_source_s* source)
{
  return source->media_stream;
}

#pragma mark - Internal utils
static int ensure_video_frame(struct file_media_source_s* stream)
{
  int ret = 0;
  if (stream->video_fifo.empty()) {
    ret = read_video_frame(stream);
  }
  return ret;
}

static void setup_media_stream(struct file_media_source_s* pthis) {
  media_stream_set_video_read(pthis->media_stream, video_read_callback, pthis);
  media_stream_set_audio_read(pthis->media_stream, audio_read_callback, pthis);
}

// convert frame time to global time, including local offset
static double video_pts_to_global_time(struct file_media_source_s* pthis,
                                       AVFrame* frame)
{
  double result = ((double)frame->pts) / (double)
  pthis->video_format_context->streams[pthis->video_stream_index]->
  time_base.den;
  result += pthis->start_offset;
  return result;
}

#pragma mark - media_stream callbacks 

int video_read_callback(struct media_stream_s* stream,
                        struct smart_frame_t** frame_out,
                        double time_clock,
                        void* p)
{
  struct file_media_source_s* pthis = (struct file_media_source_s*)p;
  time_clock += pthis->video_ts_offset;
  int ret = ensure_video_frame(pthis);
  if (ret) {
    *frame_out = NULL;
    return ret;
  }
  assert(!pthis->video_fifo.empty());
  smart_frame_t* smart_front = pthis->video_fifo.front();
  AVFrame* front = smart_frame_get(smart_front);
  double offset_frame_time = video_pts_to_global_time(pthis, front);

  // pop frames off the fifo until we're caught up
  while (offset_frame_time < time_clock) {
    smart_frame_release(smart_front);
    pthis->video_fifo.pop();
    if (ensure_video_frame(pthis)) {
      return -1;
    }
    smart_front = pthis->video_fifo.front();
    front = smart_frame_get(smart_front);
    offset_frame_time = video_pts_to_global_time(pthis, front);
  }

  // after a certain point, we'll stop duplicating frames.
  // TODO: make this configurable maybe?
  if (time_clock - offset_frame_time > 3000) {
    *frame_out = NULL;
  } else {
    *frame_out = smart_front;
  }

  return (NULL == *frame_out);
}

int audio_read_callback(struct media_stream_s* stream,
                        AVFrame* frame, double clock_time,
                        void* p)
{
  struct file_media_source_s* pthis = (struct file_media_source_s*)p;
  int ret = 0;

  assert(frame->format == AV_SAMPLE_FMT_S16);
  ret = file_audio_source_get_next(pthis->audio_source,
                                   frame->nb_samples,
                                   (int16_t*)frame->data[0]);
  return ret;
}
