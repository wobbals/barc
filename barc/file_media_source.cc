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
#include <libavutil/audio_fifo.h>

#include "file_media_source.h"
}

#include <deque>
#include <queue>

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

  std::queue<struct smart_frame_t*> video_fifo;
  std::deque<AVFrame*> audio_frame_fifo;
  AVAudioFifo* audio_sample_fifo;
  int64_t audio_last_pts;

  struct media_stream_s* media_stream;
};

int file_media_source_alloc(struct file_media_source_s** media_source_out)
{
  struct file_media_source_s* pthis = (struct file_media_source_s*)
  calloc(1, sizeof(struct file_media_source_s));
  media_stream_alloc(&pthis->media_stream);
  pthis->audio_frame_fifo = std::deque<AVFrame*>();
  pthis->video_fifo = std::queue<struct smart_frame_t*>();
  // should this be dynamically sized?
  pthis->audio_sample_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 4098);
  *media_source_out = pthis;
  return 0;
}

void file_media_source_free(struct file_media_source_s* pthis) {
  while (!pthis->audio_frame_fifo.empty()) {
    AVFrame* frame = pthis->audio_frame_fifo.front();
    pthis->audio_frame_fifo.pop_front();
    av_frame_free(&frame);
  }
  while (!pthis->video_fifo.empty()) {
    smart_frame_release(pthis->video_fifo.front());
    pthis->video_fifo.pop();
  }
  av_audio_fifo_free(pthis->audio_sample_fifo);
  avcodec_close(pthis->video_context);
  avcodec_close(pthis->audio_context);
  avformat_close_input(&pthis->audio_format_context);
  avformat_close_input(&pthis->video_format_context);
  media_stream_free(pthis->media_stream);
  free(pthis);
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

static void insert_silence(struct file_media_source_s* pthis,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts)
{
  AVFrame* silence = av_frame_alloc();
  silence->sample_rate = pthis->audio_context->sample_rate;
  silence->nb_samples = (int)num_samples;
  silence->format = pthis->audio_context->sample_fmt;
  silence->channel_layout = pthis->audio_context->channel_layout;
  av_frame_get_buffer(silence, 1);
  for (int i = 0; i < silence->channels; i++) {
    memset(silence->data[i], 0,
           silence->nb_samples *
           av_get_bytes_per_sample((enum AVSampleFormat)silence->format));
  }
  silence->pts = from_pts;
  silence->pkt_duration = to_pts - from_pts;

  pthis->audio_frame_fifo.push_front(silence);
}

static int read_audio_frame(struct file_media_source_s* pthis)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };

  /* pump packet reader until fifo is populated, or file ends */
  while (pthis->audio_frame_fifo.empty()) {
    ret = av_read_frame(pthis->audio_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = av_frame_alloc();
    got_frame = 0;
    ret = avcodec_decode_audio4(pthis->audio_context, frame,
                                &got_frame, &packet);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Error decoding audio: %s\n",
             av_err2str(ret));
    }

    if (got_frame) {
      frame->pts = av_frame_get_best_effort_timestamp(frame);
      pthis->audio_frame_fifo.push_back(frame);
    }

    av_packet_unref(&packet);
  }

  return !got_frame;
}

// shortcut for frequent checks to the frame fifo
static int ensure_audio_frames(struct file_media_source_s* stream) {
  int ret = 0;
  if (stream->audio_frame_fifo.empty()) {
    ret = read_audio_frame(stream);
  }
  return ret;
}

// ensures contiguous frames available to the sample fifo
static int audio_frame_fifo_pop(struct file_media_source_s* pthis) {
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
  AVFrame* new_frame = pthis->audio_frame_fifo.front();
  // insert silence if we detect a lapse in audio continuity
  if ((new_frame->pts - old_frame->pts) > old_frame->pkt_duration) {
    int64_t num_samples =
    samples_per_pts(pthis->audio_context->sample_rate,
                    new_frame->pts - old_frame->pts -
                    old_frame->pkt_duration,
                    pthis->audio_context->time_base);
    if (num_samples > 0) {
      printf("data gap detected at %lld. generate %lld silent samples\n",
             old_frame->pts, num_samples);
      insert_silence(pthis, num_samples, old_frame->pts, new_frame->pts);
    }
  }

  av_frame_free(&old_frame);

  return ret;
}

// pops frames off the frame fifo and copies samples to sample fifo
static int get_more_audio_samples(struct file_media_source_s* stream) {
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

  /* pump packet reader until fifo is populated, or file ends */
  while (pthis->video_fifo.empty()) {
    ret = av_read_frame(pthis->video_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = NULL;
    got_frame = 0;
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

    av_packet_unref(&packet);
  }

  return !got_frame;
}

// just for posterity -- delete this
int old_media_stream_audio_read(struct file_media_source_s* pthis,
                                double clock_time,
                                int64_t num_samples,
                                int16_t** samples_out)
{

  int ret = 0;
  int64_t local_ts = clock_time * pthis->audio_context->sample_rate;
  int64_t requested_pts_interval =
  num_samples / pthis->audio_context->sample_rate;
  // TODO: This needs to be aware of the sample rate and format of the
  // receiver
  printf("pop %lld time units of audio samples (%lld total) for local ts %lld\n",
         requested_pts_interval, num_samples, local_ts);
  printf("audio fifo size before: %d\n",
         av_audio_fifo_size(pthis->audio_sample_fifo));
  while (num_samples > av_audio_fifo_size(pthis->audio_sample_fifo) && !ret)
  {
    ret = get_more_audio_samples(pthis);
  }
  printf("audio fifo size after: %d\n",
         av_audio_fifo_size(pthis->audio_sample_fifo));

  if (ret) {
    return ret;
  }

  ret = av_audio_fifo_read(pthis->audio_sample_fifo,
                           (void**)samples_out, num_samples);
  assert(ret == num_samples);
  printf("pop %lld audio samples for local ts %lld %s\n",
         num_samples, local_ts, media_stream_get_name(pthis->media_stream));
  int64_t offset_ts = pthis->audio_last_pts + pthis->start_offset;
  int clock_drift = (int) (local_ts - offset_ts);
  printf("stream %s audio clock drift=%d local_ts=%lld offset_ts=%lld\n",
         media_stream_get_name(pthis->media_stream), clock_drift, local_ts,
         offset_ts);
  if (clock_drift > 1000) {
    // global clock is ahead of the stream. truncate some data (better yet,
    // squish some samples together
  } else if (clock_drift < -1000) {
    // stream is ahead of the global clock. introduce some silence or
    // spread samples apart
  }
  return ret;
}

void file_media_get_video_frame_callback() {
/*
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
*/
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
  struct file_media_source_s* pthis;
  file_media_source_alloc(&pthis);
  pthis->start_offset = start_offset;
  pthis->stop_offset = stop_offset;
  pthis->filename = filename;

  ret = avformat_open_input(&pthis->audio_format_context, filename,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  ret = avformat_open_input(&pthis->video_format_context, filename,
                            NULL, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  // does this actually work? i don't think so.
  pthis->audio_context->request_sample_fmt = AV_SAMPLE_FMT_S16;

  archive_open_codec(pthis->video_format_context,
                     AVMEDIA_TYPE_VIDEO,
                     &pthis->video_context,
                     &pthis->video_stream_index);
  archive_open_codec(pthis->audio_format_context,
                     AVMEDIA_TYPE_AUDIO,
                     &pthis->audio_context,
                     &pthis->audio_stream_index);

  assert(pthis->audio_context->sample_fmt = AV_SAMPLE_FMT_S16);

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

int file_stream_is_active_at_time(struct file_media_source_s* pthis,
                                  double clock_time)
{
  return (pthis->start_offset <= clock_time &&
          clock_time < pthis->stop_offset);
}

int64_t file_stream_get_stop_offset(struct file_media_source_s* pthis)
{
  return pthis->stop_offset;
}

int64_t file_stream_get_start_offset(struct file_media_source_s* pthis)
{
  return pthis->start_offset;
}

struct media_stream_s* file_media_source_get_stream
(struct file_media_source_s* source)
{
  return source->media_stream;
}
