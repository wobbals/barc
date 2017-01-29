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
}

#include <queue>

static const AVRational global_time_base = {1, 1000};

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
    std::queue<AVFrame*> audio_fifo;

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

    stream->video_fifo = std::queue<AVFrame*>();
    stream->audio_fifo = std::queue<AVFrame*>();
    stream->start_offset = start_offset;
    stream->stop_offset = stop_offset;
    stream->sz_name = stream_name;
    stream->sz_class = stream_class;

    return 0;
}

int archive_stream_free(struct archive_stream_t* stream)
{
    while (!stream->video_fifo.empty()) {
        AVFrame* frame = stream->video_fifo.front();
        stream->video_fifo.pop();
        av_frame_free(&frame);
    }
    while (!stream->audio_fifo.empty()) {
        AVFrame* frame = stream->audio_fifo.front();
        stream->audio_fifo.pop();
        av_frame_free(&frame);
    }
    avcodec_close(stream->video_context);
    avcodec_close(stream->audio_context);
    avformat_close_input(&stream->format_context);
    free(stream);
    return 0;
}

static int get_next_frame(struct archive_stream_t* stream)
{
    int ret, got_frame = 0;
    AVPacket packet = { 0 };

    /* pump packet reader until both fifos are populated */
    while (stream->audio_fifo.empty() || stream->video_fifo.empty()) {
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
            //printf("audio pts %lld\n", packet.pts);
            got_frame = 0;
            ret = avcodec_decode_audio4(stream->audio_context, frame,
                                        &got_frame, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding audio: %s\n",
                       av_err2str(ret));
            }

            if (got_frame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                stream->audio_fifo.push(frame);
            }
        } else {
            av_frame_free(&frame);
        }

        av_packet_unref(&packet);
    }

    return !got_frame;
}

int archive_stream_peek_frame(struct archive_stream_t* stream,
                              AVFrame** frame,
                              int64_t* offset_pts,
                              enum AVMediaType media_type)
{
    std::queue<AVFrame*> queue;
    AVCodecContext* codec_context;
    if (AVMEDIA_TYPE_AUDIO == media_type) {
        queue = stream->audio_fifo;
        codec_context = stream->audio_context;
    } else if (AVMEDIA_TYPE_VIDEO == media_type) {
        queue = stream->video_fifo;
        codec_context = stream->video_context;
    } else {
        printf("No queue for media type %d\n", media_type);
        return -1;
    }


    if (!queue.empty()) {
        *frame = queue.front();
        *offset_pts = av_rescale_q((*frame)->pts,
                                   codec_context->time_base,
                                   global_time_base) + stream->start_offset;
        return 0;
    } else if (get_next_frame(stream)) {
        *frame = NULL;
        *offset_pts = -1;
        return AVERROR_EOF;
    } else {
        // try again
        return archive_stream_peek_frame(stream, frame, offset_pts, media_type);
    }
}

int archive_stream_pop_frame(struct archive_stream_t* stream,
                                   AVFrame** frame,
                                   int64_t* offset_pts,
                                   enum AVMediaType media_type)
{
    int ret = archive_stream_peek_frame(stream, frame, offset_pts, media_type);
    if (ret < 0) {
        return ret;
    }
    if (AVMEDIA_TYPE_VIDEO == media_type) {
        stream->video_fifo.pop();
    } else if (AVMEDIA_TYPE_AUDIO == media_type) {
        stream->audio_fifo.pop();
    }  else {
        printf("Unknown media type for pop %d\n", media_type);
        return -1;
    }
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
