//
//  archive_stream.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

extern "C" {
#include "archive_stream.h"
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

#include <queue>
#include <deque>

static int ensure_audio_frames(struct archive_stream_t* stream);
static inline int64_t samples_per_pts(int sample_rate, int64_t pts,
                                      AVRational time_base);
static inline float pts_per_sample(float sample_rate, float num_samples,
                                   AVRational time_base);
static void insert_silence(struct archive_stream_t* stream,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts);

struct archive_stream_t {
  // media sources
  archive_read_packet_cb* audio_read_cb;
  void* audio_read_arg;
  archive_read_packet_cb* video_read_cb;
  void* video_read_arg;
  archive_get_config_cb* audio_config_cb;
  void* audio_config_arg;
  archive_get_config_cb* video_config_cb;
  void* video_config_arg;

    std::queue<struct smart_frame_t*> video_fifo;
    std::deque<AVFrame*> audio_frame_fifo;
    AVAudioFifo* audio_sample_fifo;
    int64_t audio_last_pts;

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
void archive_stream_set_video_read(struct archive_stream_t* stream,
                                   archive_read_packet_cb* cb, void* p)
{
  stream->video_read_cb = cb;
  stream->video_read_arg = p;
}

void archive_stream_set_audio_read(struct archive_stream_t* stream,
                                   archive_read_packet_cb* cb, void* p)
{
  stream->audio_read_cb = cb;
  stream->audio_read_arg = p;
}

void archive_stream_set_audio_config_callback
(struct archive_stream_t* stream, archive_get_config_cb* cb, void* p)
{
  stream->audio_config_cb = cb;
  stream->audio_read_arg = p;
}

void archive_stream_set_video_config_callback
(struct archive_stream_t* stream, archive_get_config_cb* cb, void* p)
{
  stream->video_config_cb = cb;
  stream->video_config_arg = p;
}

# pragma mark - memory lifecycle
int archive_stream_alloc(struct archive_stream_t** stream_out,
                         const char* stream_name,
                         const char* stream_class)
{
  struct archive_stream_t* stream =
  (struct archive_stream_t*) calloc(1, sizeof(struct archive_stream_t));
  stream->sz_name = stream_name;
  stream->sz_class = stream_class;
  stream->video_fifo = std::queue<struct smart_frame_t*>();
  stream->audio_frame_fifo = std::deque<AVFrame*>();
  // should this be dynamically sized?
  stream->audio_sample_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 4098);
  *stream_out = stream;
  return 0;
}


int archive_stream_free(struct archive_stream_t* stream)
{
    while (!stream->video_fifo.empty()) {
        smart_frame_t* frame = stream->video_fifo.front();
        stream->video_fifo.pop();
        smart_frame_release(frame);
    }
    while (!stream->audio_frame_fifo.empty()) {
        AVFrame* frame = stream->audio_frame_fifo.front();
        stream->audio_frame_fifo.pop_front();
        av_frame_free(&frame);
    }
    av_audio_fifo_free(stream->audio_sample_fifo);
    free(stream);
    return 0;
}

#pragma mark - Video stream management

/* Editorial: Splitting the input reader into two separate instances allows
 * us to seek through the source file for specific data without altering
 * progress of other stream tracks. The alternative is to seek once and keep
 * extra data in a fifo. Sometimes this causes memory to run away, so instead
 * we just read the same input file twice.
 */
static int read_video_frame(struct archive_stream_t* stream)
{
  int ret, got_frame = 0;
  AVPacket packet = { 0 };
  struct stream_config_s stream_config = {0};
  stream->video_config_cb(&stream_config, stream->video_config_arg);

  /* pump packet reader until fifo is populated, or file ends */
  while (stream->video_fifo.empty()) {
    ret = stream->video_read_cb(stream, &packet, stream->video_read_arg);
    //ret = av_read_frame(stream->video_format_context, &packet);
    if (ret < 0) {
      return ret;
    }

    AVFrame* frame = av_frame_alloc();
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

static int ensure_video_frame(struct archive_stream_t* stream) {
    int ret = 0;
    if (stream->video_fifo.empty()) {
        ret = read_video_frame(stream);
    }
    return ret;
}

int archive_stream_get_video_for_time(struct archive_stream_t* stream,
                                      smart_frame_t** smart_frame,
                                      int64_t clock_time,
                                      AVRational clock_time_base)
{
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

int archive_stream_has_video_for_time(struct archive_stream_t* stream,
                                      int64_t clock_time,
                                      AVRational clock_time_base)
{
    struct smart_frame_t* frame;
    return 0 == archive_stream_get_video_for_time(stream, &frame,
                                                  clock_time, clock_time_base);
}

static void insert_silence(struct archive_stream_t* stream,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts)
{
    AVFrame* silence = av_frame_alloc();
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);
  silence->sample_rate = stream_config.sample_rate;
    silence->nb_samples = (int)num_samples;
    silence->format = stream_config.sample_format;
    silence->channel_layout = stream_config.channel_layout;
    av_frame_get_buffer(silence, 1);
    for (int i = 0; i < silence->channels; i++) {
        memset(silence->data[i], 0,
               silence->nb_samples *
               av_get_bytes_per_sample((enum AVSampleFormat)silence->format));
    }
    silence->pts = from_pts;
    silence->pkt_duration = to_pts - from_pts;

    stream->audio_frame_fifo.push_front(silence);
}

static int read_audio_frame(struct archive_stream_t* stream)
{
    int ret, got_frame = 0;
    AVPacket packet = { 0 };
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);

    /* pump packet reader until fifo is populated, or file ends */
    while (stream->audio_frame_fifo.empty()) {
      ret = stream->audio_read_cb(stream, &packet, stream->audio_read_arg);
      //ret = av_read_frame(stream->audio_format_context, &packet);
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
static int ensure_audio_frames(struct archive_stream_t* stream) {
    int ret = 0;
    if (stream->audio_frame_fifo.empty()) {
        ret = read_audio_frame(stream);
    }
    return ret;
}

// ensures contiguous frames available to the sample fifo
static int audio_frame_fifo_pop(struct archive_stream_t* stream) {
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
static int get_more_audio_samples(struct archive_stream_t* stream) {
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

int archive_stream_pop_audio_samples(struct archive_stream_t* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out,
                                     int64_t clock_time,
                                     AVRational time_base)
{
    assert(48000 == sample_rate);
    assert(format == AV_SAMPLE_FMT_S16);
    int ret = 0;
  struct stream_config_s stream_config = {0};
  stream->audio_config_cb(&stream_config, stream->audio_config_arg);
  AVRational audio_time_base = {0};
  audio_time_base.num = 1;
  audio_time_base.den = stream_config.sample_rate;
    int64_t local_ts = av_rescale_q(clock_time, time_base,
                                    audio_time_base);
    int requested_pts_interval = pts_per_sample(sample_rate, num_samples,
                                            audio_time_base);
    int fifo_size = av_audio_fifo_size(stream->audio_sample_fifo);
    float fifo_pts_length = pts_per_sample(sample_rate, fifo_size,
                                           audio_time_base);
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
    printf("audio fifo size after: %d\n", av_audio_fifo_size(stream->audio_sample_fifo));

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

static inline float pts_per_sample(float sample_rate, float num_samples,
                            AVRational time_base)
{
    // (duration * time_base) * (sample_rate) == sample_count
    // (20 / 1000) * 48000 == 960
    return
    num_samples * ((float)time_base.den / (float)time_base.num) / sample_rate;
}

#pragma mark - Getters & Setters

int archive_stream_get_offset_x(struct archive_stream_t* stream) {
    return stream->x_offset;
}

int archive_stream_get_offset_y(struct archive_stream_t* stream) {
    return stream->y_offset;
}

int archive_stream_get_render_width(struct archive_stream_t* stream) {
    return stream->render_width;
}

int archive_stream_get_render_height(struct archive_stream_t* stream) {
    return stream->render_height;
}

int archive_stream_get_z_index(const struct archive_stream_t* stream) {
    return stream->z_index;
}

enum object_fit archive_stream_get_object_fit(struct archive_stream_t* stream) {
    return stream->object_fit;
}

void archive_stream_set_offset_x(struct archive_stream_t* stream,
                                 int x_offset)
{
    stream->x_offset = x_offset;
}

void archive_stream_set_offset_y(struct archive_stream_t* stream,
                                 int y_offset)
{
    stream->y_offset = y_offset;
}

void archive_stream_set_render_width(struct archive_stream_t* stream,
                                     int width)
{
    stream->render_width = width;
}

void archive_stream_set_render_height(struct archive_stream_t* stream,
                                      int height)
{
    stream->render_height = height;
}

void archive_stream_set_z_index(struct archive_stream_t* stream,
                                int z_index)
{
    stream->z_index = z_index;
}


void archive_stream_set_object_fit(struct archive_stream_t* stream,
                                   enum object_fit object_fit)
{
    stream->object_fit = object_fit;
}

const char* archive_stream_get_name(struct archive_stream_t* stream) {
    return stream->sz_name;
}

const char* archive_stream_get_class(struct archive_stream_t* stream) {
    return stream->sz_class;
}
