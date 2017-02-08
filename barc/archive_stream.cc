//
//  archive_stream.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

extern "C" {
#include "archive_stream.h"
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <assert.h>
}

#include <queue>
#include <deque>

static int ensure_audio_frames(struct archive_stream_t* stream);
static inline int64_t samples_per_pts(int sample_rate, int64_t pts,
                                      AVRational time_base);
static void insert_silence(struct archive_stream_t* stream,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts);

// audio PTS are presented in millis, but where is this declared in ffmpeg?
static const AVRational millisecond_base = { 1, 1000 };

struct archive_stream_t {
    int64_t start_offset;
    int64_t stop_offset;
    int64_t duration;
    AVFormatContext* format_context;
    AVCodecContext* video_context;
    AVCodecContext* audio_context;
    int video_stream_index;
    int audio_stream_index;
    std::queue<AVFrame*> video_fifo;
    std::deque<AVFrame*> audio_frame_fifo;

    AVAudioFifo* audio_sample_fifo;

    const char* sz_name;
    const char* sz_class;
    int source_width;
    int source_height;
    int x_offset;
    int y_offset;
    int render_width;
    int render_height;
};

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

    /* init the video decoder */
    ret = avcodec_open2(*codec_context, dec, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
        return ret;
    }

    return 0;
}

int archive_stream_open(struct archive_stream_t** stream_out,
                        const char *filename,
                        int64_t start_offset, int64_t stop_offset,
                        const char* stream_name,
                        const char* stream_class)
{
    int ret;
    struct archive_stream_t* stream =
    (struct archive_stream_t*) calloc(1, sizeof(struct archive_stream_t));
    *stream_out = stream;

    ret = avformat_open_input(&stream->format_context, filename,
                              NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    ret = avformat_find_stream_info(stream->format_context, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    archive_open_codec(stream->format_context,
                       AVMEDIA_TYPE_VIDEO,
                       &stream->video_context,
                       &stream->video_stream_index);

    archive_open_codec(stream->format_context,
                       AVMEDIA_TYPE_AUDIO,
                       &stream->audio_context,
                       &stream->audio_stream_index);

    assert(stream->audio_context->sample_fmt = AV_SAMPLE_FMT_S16);
    stream->audio_context->request_sample_fmt = AV_SAMPLE_FMT_S16;

    stream->video_fifo = std::queue<AVFrame*>();
    stream->audio_frame_fifo = std::deque<AVFrame*>();
    stream->start_offset = start_offset;
    stream->stop_offset = stop_offset;
    stream->sz_name = stream_name;
    stream->sz_class = stream_class;

    // should this be dynamic?
    stream->audio_sample_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 4098);

    ret = ensure_audio_frames(stream);
    if (ret) {
        return ret;
    }

    // add silence to head of queue before the first audio packet plays out
    AVFrame* frame = stream->audio_frame_fifo.front();
    if (frame->pts > 0) {
        int64_t num_samples =
        samples_per_pts(stream->audio_context->sample_rate,
                        frame->pts,
                        millisecond_base);
        if (num_samples > 0) {
            insert_silence(stream, num_samples, 0, frame->pts);
        }
    }

    return 0;
}

int archive_stream_free(struct archive_stream_t* stream)
{
    while (!stream->video_fifo.empty()) {
        AVFrame* frame = stream->video_fifo.front();
        stream->video_fifo.pop();
        av_frame_free(&frame);
    }
    while (!stream->audio_frame_fifo.empty()) {
        AVFrame* frame = stream->audio_frame_fifo.front();
        stream->audio_frame_fifo.pop_front();
        av_frame_free(&frame);
    }
    avcodec_close(stream->video_context);
    avcodec_close(stream->audio_context);
    avformat_close_input(&stream->format_context);
    av_audio_fifo_free(stream->audio_sample_fifo);
    free(stream);
    return 0;
}

static int get_next_frame(struct archive_stream_t* stream)
{
    int ret, got_frame = 0;
    AVPacket packet = { 0 };

    /* pump packet reader until both fifos are populated */
    while (stream->audio_frame_fifo.empty() || stream->video_fifo.empty()) {
        ret = av_read_frame(stream->format_context, &packet);
        if (ret < 0) {
            return ret;
        }

        AVFrame* frame = av_frame_alloc();
        if (packet.stream_index == stream->video_stream_index) {
            got_frame = 0;
            ret = avcodec_decode_video2(stream->video_context, frame,
                                        &got_frame, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding video: %s\n",
                       av_err2str(ret));
            }
            
            if (got_frame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                stream->video_fifo.push(frame);
            }
        } else if (packet.stream_index == stream->audio_stream_index) {
            got_frame = 0;
            ret = avcodec_decode_audio4(stream->audio_context, frame,
                                        &got_frame, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding audio: %s\n",
                       av_err2str(ret));
            }

            if (got_frame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                stream->audio_frame_fifo.push_back(frame);
            }
        } else {
            av_frame_free(&frame);
        }

        av_packet_unref(&packet);
    }

    return !got_frame;
}

int archive_stream_peek_video(struct archive_stream_t* stream,
                              AVFrame** frame,
                              int64_t* offset_pts)
{
    std::queue<AVFrame*> queue = stream->video_fifo;

    if (!queue.empty()) {
        *frame = queue.front();
        //printf("%p peek %d pts %lld\n", stream, media_type, (*frame)->pts);
        *offset_pts = (*frame)->pts + stream->start_offset;
        // round down to the nearest packet interval for easier math
        if ((*frame)->pkt_duration > 0) {
            int64_t round = *offset_pts % ((*frame)->pkt_duration);
            *offset_pts -= round;
        }
        return 0;
    } else if (get_next_frame(stream)) {
        *frame = NULL;
        *offset_pts = -1;
        return AVERROR_EOF;
    } else {
        // try again. hopefully we should not need to pump many frames.
        return archive_stream_peek_video(stream, frame, offset_pts);
    }
}

int archive_stream_pop_video(struct archive_stream_t* stream,
                             AVFrame** frame,
                             int64_t* offset_pts)
{
    int ret = archive_stream_peek_video(stream, frame, offset_pts);
    if (ret < 0) {
        return ret;
    }
    stream->video_fifo.pop();
    return ret;
}

static void insert_silence(struct archive_stream_t* stream,
                           int64_t num_samples,
                           int64_t from_pts, int64_t to_pts)
{
    AVFrame* silence = av_frame_alloc();
    silence->sample_rate = stream->audio_context->sample_rate;
    silence->nb_samples = (int)num_samples;
    silence->format = stream->audio_context->sample_fmt;
    silence->channel_layout = stream->audio_context->channel_layout;
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

static inline int64_t samples_per_pts(int sample_rate, int64_t pts,
                                      AVRational time_base)
{
    // (duration * time_base) * (sample_rate) == sample_count
    // (20 / 1000) * 48000 == 960
    return (float)((float)pts * (float)time_base.num) / (float)time_base.den * (float)sample_rate;
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

// shortcut for frequent checks to the frame fifo
static int ensure_audio_frames(struct archive_stream_t* stream) {
    int ret = 0;
    if (stream->audio_frame_fifo.empty()) {
        ret = get_next_frame(stream);
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

    AVFrame* new_frame = stream->audio_frame_fifo.front();
    // insert silence if we detect a lapse in audio continuity
    if ((new_frame->pts - old_frame->pts) > old_frame->pkt_duration) {
        int64_t num_samples =
        samples_per_pts(stream->audio_context->sample_rate,
                        new_frame->pts - old_frame->pts -
                        old_frame->pkt_duration,
                        millisecond_base);
        if (num_samples > 0) {
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
        ret = av_audio_fifo_write(stream->audio_sample_fifo,
                                  (void**)frame->data, frame->nb_samples);
    }
    ret = audio_frame_fifo_pop(stream);
    return ret;
}

int archive_stream_pop_audio_samples(struct archive_stream_t* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out)
{
    int ret = 0;

    // TODO: This needs to be aware of the sample rate and format of the
    // receiver
    assert(48000 == sample_rate);
    assert(format == AV_SAMPLE_FMT_S16);

    while (num_samples > av_audio_fifo_size(stream->audio_sample_fifo) && !ret)
    {
        ret = get_more_audio_samples(stream);
    }

    if (ret) {
        printf("can't get more samples");
        return ret;
    }

    ret = av_audio_fifo_read(stream->audio_sample_fifo,
                             (void**)samples_out, num_samples);

    return ret;
}

int archive_stream_is_active_at_time(struct archive_stream_t* stream,
                                     int64_t global_time)
{
    return (stream->start_offset <= global_time &&
            global_time < stream->stop_offset);
}

int* archive_stream_offset_x(struct archive_stream_t* stream) {
    return &stream->x_offset;
}

int* archive_stream_offset_y(struct archive_stream_t* stream) {
    return &stream->y_offset;
}

int* archive_stream_render_width(struct archive_stream_t* stream) {
    return &stream->render_width;
}

int* archive_stream_render_height(struct archive_stream_t* stream) {
    return &stream->render_height;
}

int64_t archive_stream_get_stop_offset(struct archive_stream_t* stream)
{
    return stream->stop_offset;
}

int64_t archive_stream_get_start_offset(struct archive_stream_t* stream)
{
    return stream->start_offset;
}

const char* archive_stream_get_name(struct archive_stream_t* stream) {
    return stream->sz_name;
}

const char* archive_stream_get_class(struct archive_stream_t* stream) {
    return stream->sz_class;
}
