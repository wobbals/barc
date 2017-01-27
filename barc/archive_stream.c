//
//  archive_stream.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include "archive_stream.h"
#include <libavutil/opt.h>

static const AVRational global_time_base = {1, 1000};

int archive_stream_open(struct archive_stream_t* stream, const char *filename,
                        int64_t start_offset, int64_t stop_offset)
{
    int ret;
    AVCodec *dec;

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

    /* select the video stream */
    ret = av_find_best_stream(stream->format_context, AVMEDIA_TYPE_VIDEO,
                              -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
        return ret;
    }
    stream->video_stream_index = ret;
    stream->decode_context =
    stream->format_context->streams[stream->video_stream_index]->codec;

    av_opt_set_int(stream->decode_context, "refcounted_frames", 1, 0);

    /* init the video decoder */
    ret = avcodec_open2(stream->decode_context, dec, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
        return ret;
    }

    stream->current_frame = av_frame_alloc();
    stream->current_frame_valid = 0;
    stream->start_offset = start_offset;
    stream->stop_offset = stop_offset;

    return 0;
}

int archive_stream_free(struct archive_stream_t* stream)
{
    avcodec_close(stream->decode_context);
    avformat_close_input(&stream->format_context);
    av_frame_free(&stream->current_frame);

    return 0;
}

int get_next_frame(struct archive_stream_t* stream)
{
    int ret, got_frame;
    AVPacket packet = { 0 };
    /* read all packets */
    while (1) {
        ret = av_read_frame(stream->format_context, &packet);
        if (ret < 0) {
            return ret;
        }

        if (packet.stream_index == stream->video_stream_index) {
            got_frame = 0;

            ret = avcodec_decode_video2(stream->decode_context,
                                        stream->current_frame,
                                        &got_frame, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding video\n");
                break;
            }
            
            if (got_frame) {
                stream->current_frame->pts =
                av_frame_get_best_effort_timestamp(stream->current_frame);
                stream->current_frame_valid = 1;
                break;
            }
        } else if (packet.stream_index == stream->audio_stream_index) {
            // TODO: handle audio
        }

        av_packet_unref(&packet);
    }

    return !got_frame;
}

int archive_stream_peek_video_frame
(struct archive_stream_t* stream, AVFrame** frame, int64_t* offset_pts)
{
    if (!stream->current_frame_valid && get_next_frame(stream)) {
        *frame = NULL;
        *offset_pts = -1;
        return AVERROR_EOF;
    } else {
        *frame = stream->current_frame;
        *offset_pts = av_rescale_q(stream->current_frame->pts,
                                   stream->decode_context->time_base,
                                   global_time_base) + stream->start_offset;
        return 0;
    }
}

int archive_stream_pop_video_frame
(struct archive_stream_t* stream, AVFrame** frame, int64_t* offset_pts)
{
    int ret = archive_stream_peek_video_frame(stream, frame, offset_pts);
    stream->current_frame_valid = 0;
    return ret;
}

int archive_stream_is_active_at_time(struct archive_stream_t* stream,
                                     int64_t global_time)
{
    return (stream->start_offset <= global_time &&
            global_time < stream->stop_offset);
}
