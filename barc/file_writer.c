//
//  file_writer.c
//  barc
//
//  Created by Charley Robinson on 2/9/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include "file_writer.h"
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <assert.h>

const int out_pix_format = AV_PIX_FMT_YUV420P;
const int out_audio_format = AV_SAMPLE_FMT_FLTP;
const int src_audio_format = AV_SAMPLE_FMT_S16;
const int out_audio_num_channels = 1;

const char *video_filter_descr = "null";
const char *audio_filter_descr = "aresample=48000,aformat=sample_fmts=s16:channel_layouts=mono";

const AVRational global_time_base = { 1, 1000 };
const int64_t out_video_fps = 30;
const int64_t out_sample_rate = 48000;

static int init_audio_filters(struct file_writer_t* file_writer,
                              const char *filters_descr);
static int init_video_filters(struct file_writer_t* file_writer,
                              const char *filters_descr,
                              int out_width, int out_height);
static int open_output_file(struct file_writer_t* file_writer,
                            const char* filename);

int file_writer_alloc(struct file_writer_t** writer) {
    struct file_writer_t* result =
    (struct file_writer_t*) calloc(1, sizeof(struct file_writer_t));
    uv_mutex_init(&result->write_lock);
    *writer = result;
    return 0;
}

void file_writer_free(struct file_writer_t* writer) {
    uv_mutex_destroy(&writer->write_lock);
    free(writer);
}

int file_writer_open(struct file_writer_t* file_writer,
                     const char* filename,
                     int out_width, int out_height)
{
    int ret;
    file_writer->out_height = out_height;
    file_writer->out_width = out_width;

    open_output_file(file_writer, filename);

    ret = init_audio_filters(file_writer, audio_filter_descr);
    if (ret < 0)
    {
        printf("Error: init audio filters\n");
        return ret;
    }

    ret = init_video_filters(file_writer, video_filter_descr,
                             out_width, out_height);
    if (ret < 0)
    {
        printf("Error: init video filters\n");
    }

    return ret;
}


static int init_audio_filters(struct file_writer_t* file_writer,
                              const char *filters_descr)
{
    char args[512];
    int ret = 0;
    AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    static enum AVSampleFormat out_sample_fmts[2];
    out_sample_fmts[0] = out_audio_format;
    out_sample_fmts[1] = -1;
    static const int64_t out_channel_layouts[] = { AV_CH_LAYOUT_MONO, -1 };
    static const int out_sample_rates[] = { 48000, -1 };
    const AVFilterLink *outlink;
    AVRational time_base = { 1, out_sample_rate };

    file_writer->audio_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !file_writer->audio_filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!file_writer->audio_ctx_out->channel_layout) {
        file_writer->audio_ctx_out->channel_layout =
        av_get_default_channel_layout(file_writer->audio_ctx_out->channels);
    }
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             time_base.num, time_base.den,
             file_writer->audio_ctx_out->sample_rate,
             av_get_sample_fmt_name(file_writer->audio_ctx_out->sample_fmt),
             file_writer->audio_ctx_out->channel_layout);
    ret = avfilter_graph_create_filter(&file_writer->audio_buffersrc_ctx,
                                       abuffersrc, "in",
                                       args, NULL,
                                       file_writer->audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        goto end;
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&file_writer->audio_buffersink_ctx,
                                       abuffersink, "out",
                                       NULL, NULL,
                                       file_writer->audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
                              "sample_fmts", out_sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
                              "channel_layouts", out_channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
                              "sample_rates", out_sample_rates, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = file_writer->audio_buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = file_writer->audio_buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(file_writer->audio_filter_graph,
                                        filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(file_writer->audio_filter_graph,
                                     NULL)) < 0)
        goto end;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = file_writer->audio_buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1,
                                 outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
           args);

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}


static int init_video_filters(struct file_writer_t* file_writer,
                              const char *filters_descr,
                              int out_width, int out_height)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    //AVRational time_base = dec_ctx->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    AVRational out_aspect_ratio = { out_width , out_height };

    file_writer->video_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !file_writer->video_filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             out_width,
             out_height,
             out_pix_format,
             global_time_base.num, global_time_base.den,
             out_aspect_ratio.num,
             out_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&file_writer->video_buffersrc_ctx,
                                       buffersrc, "in",
                                       args, NULL,
                                       file_writer->video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&file_writer->video_buffersink_ctx,
                                       buffersink, "out",
                                       NULL, NULL,
                                       file_writer->video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->video_buffersink_ctx,
                              "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = file_writer->video_buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = file_writer->video_buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(file_writer->video_filter_graph,
                                        filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(file_writer->video_filter_graph,
                                     NULL)) < 0)
        goto end;

    printf("%s\n", avfilter_graph_dump(file_writer->video_filter_graph, NULL));
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 */
static int init_resampler(AVCodecContext *input_codec_context,
                          AVCodecContext *output_codec_context,
                          SwrContext **resample_context)
{
    int error;

    /**
     * Create a resampler context for the conversion.
     * Set the conversion parameters.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity (they are sometimes not detected
     * properly by the demuxer and/or decoder).
     */
    *resample_context =
    swr_alloc_set_opts(NULL,
                       av_get_default_channel_layout
                       (output_codec_context->channels),
                       output_codec_context->sample_fmt,
                       output_codec_context->sample_rate,
                       av_get_default_channel_layout
                       (input_codec_context->channels),
                       input_codec_context->sample_fmt,
                       input_codec_context->sample_rate,
                       0, NULL);
    if (!*resample_context) {
        fprintf(stderr, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }
    /**
     * Perform a sanity check so that the number of converted samples is
     * not greater than the number of samples to be converted.
     * If the sample rates differ, this case has to be handled differently
     */
    assert(output_codec_context->sample_rate ==
           input_codec_context->sample_rate);

    /** Open the resampler with the specified parameters. */
    if ((error = swr_init(*resample_context)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(resample_context);
        return error;
    }
    return 0;
}

static int open_output_file(struct file_writer_t* file_writer,
                            const char* filename)
{
    AVDictionary *opt = NULL;
    int ret;

    /* allocate the output media context */
    avformat_alloc_output_context2(&file_writer->format_ctx_out,
                                   NULL, NULL, filename);
    if (!file_writer->format_ctx_out) {
        printf("Could not deduce output format from file extension.\n");
        avformat_alloc_output_context2(&file_writer->format_ctx_out,
                                       NULL, "mpeg", filename);
    }
    // fall back to mpeg
    if (!file_writer->format_ctx_out) {
        printf("Could not allocate format output context");
        exit(1);
    }

    av_dump_format(file_writer->format_ctx_out, 0, filename, 1);

    AVOutputFormat* fmt = file_writer->format_ctx_out->oformat;

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&file_writer->format_ctx_out->pb,
                        filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            exit(1);
        }
    }

    /* find the video encoder */
    file_writer->video_codec_out = avcodec_find_encoder(fmt->video_codec);
    if (!file_writer->video_codec_out) {
        printf("Video codec not found\n");
        exit(1);
    }

    file_writer->audio_codec_out = avcodec_find_encoder(fmt->audio_codec);
    if (!file_writer->audio_codec_out) {
        printf("Audio codec not found\n");
        exit(1);
    }

    file_writer->video_stream =
    avformat_new_stream(file_writer->format_ctx_out,
                        file_writer->video_codec_out);
    file_writer->audio_stream =
    avformat_new_stream(file_writer->format_ctx_out,
                        file_writer->audio_codec_out);

    file_writer->video_ctx_out = file_writer->video_stream->codec;
    if (!file_writer->video_ctx_out) {
        printf("Could not allocate video codec context\n");
        exit(1);
    }
    file_writer->audio_ctx_out = file_writer->audio_stream->codec;
    if (!file_writer->audio_ctx_out) {
        printf("Could not allocate audio codec context\n");
        exit(1);
    }

    file_writer->audio_stream->time_base.num = 1;
    file_writer->audio_stream->time_base.den = out_sample_rate;

    // Codec configuration
    file_writer->audio_ctx_out->bit_rate = 96000;
    file_writer->audio_ctx_out->sample_fmt = out_audio_format;
    file_writer->audio_ctx_out->sample_rate = out_sample_rate;
    file_writer->audio_ctx_out->channels = 1;
    file_writer->audio_ctx_out->channel_layout = AV_CH_LAYOUT_MONO;

    /* put sample parameters */
    file_writer->video_ctx_out->qmin = 20;
    /* resolution must be a multiple of two */
    file_writer->video_ctx_out->width = file_writer->out_width;
    file_writer->video_ctx_out->height = file_writer->out_height;
    file_writer->video_ctx_out->pix_fmt = out_pix_format;
    file_writer->video_ctx_out->time_base = global_time_base;
    //video_ctx_out->max_b_frames = 1;

    if (fmt->video_codec == AV_CODEC_ID_H264) {
        av_opt_set(file_writer->video_ctx_out->priv_data,
                   "preset", "fast", 0);
    }

    /* Some formats want stream headers to be separate. */
    if (file_writer->format_ctx_out->oformat->flags & AVFMT_GLOBALHEADER) {
        file_writer->video_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;
        file_writer->audio_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    /* open the context */
    if (avcodec_open2(file_writer->video_ctx_out,
                      file_writer->video_codec_out, NULL) < 0) {
        printf("Could not open video codec\n");
        exit(1);
    }

    /* open the context */
    if (avcodec_open2(file_writer->audio_ctx_out,
                      file_writer->audio_codec_out, NULL) < 0) {
        printf("Could not open audio codec\n");
        exit(1);
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(file_writer->format_ctx_out, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        exit(1);
    }

    printf("Ready to encode video file %s\n", filename);

    return 0;
}

static int safe_write_packet(struct file_writer_t* file_writer,
                             AVPacket* packet)
{
    uv_mutex_lock(&file_writer->write_lock);
    int ret = av_interleaved_write_frame(file_writer->format_ctx_out, packet);
    uv_mutex_unlock(&file_writer->write_lock);
    return ret;
}

static int write_audio_frame(struct file_writer_t* file_writer,
                      AVFrame* frame)
{
    int got_packet, ret;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the frame */
    ret = avcodec_encode_audio2(file_writer->audio_ctx_out,
                                &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
        return 1;
    }

    if (got_packet) {
        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&pkt, file_writer->audio_ctx_out->time_base,
                             file_writer->audio_stream->time_base);
        pkt.stream_index = file_writer->audio_stream->index;

        /* Write the compressed frame to the media file. */
        printf("Write audio frame %lld, size=%d pts=%lld duration=%lld\n",
               file_writer->audio_frame_ct, pkt.size, pkt.pts, pkt.duration);
        file_writer->audio_frame_ct++;
        ret = safe_write_packet(file_writer, &pkt);
        if (ret) {
            printf("tilt");
        }
    } else {
        ret = 0;
    }
    av_free_packet(&pkt);
    return ret;
}

// inserts audio frame into filtergraph
int file_writer_push_audio_frame(struct file_writer_t* file_writer,
                                 AVFrame* frame)
{
    int ret;
    AVFrame *filt_frame = av_frame_alloc();
    ret = av_buffersrc_add_frame_flags(file_writer->audio_buffersrc_ctx,
                                       frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
    }

    /* pull filtered audio from the filtergraph */
    while (0 == ret) {
        ret = av_buffersink_get_frame(file_writer->audio_buffersink_ctx,
                                      filt_frame);
        if (ret < 0) {
            break;
        }
        ret = write_audio_frame(file_writer, filt_frame);
        av_frame_unref(filt_frame);
    }
    av_frame_free(&filt_frame);
    return ret;
}

static int write_video_frame(struct file_writer_t* file_writer,
                             AVFrame* frame)
{
    int got_packet, ret;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(file_writer->video_ctx_out,
                                &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
        exit(1);
    }

    if (got_packet) {
        /* 
         rescale output packet timestamp values from codec to stream timebase
         */
        av_packet_rescale_ts(&pkt, global_time_base,
                             file_writer->video_stream->time_base);
        pkt.stream_index = file_writer->video_stream->index;

        /* Write the compressed frame to the media file. */
        printf("Write video frame %lld, size=%d pts=%lld\n",
               file_writer->video_frame_ct, pkt.size, pkt.pts);
        file_writer->video_frame_ct++;
        ret = safe_write_packet(file_writer, &pkt);
        if (ret) {
            printf("tilt");
        }

    } else {
        ret = 0;
    }

    av_free_packet(&pkt);

    return ret;
}

int file_writer_close(struct file_writer_t* file_writer)
{
    int ret = av_write_trailer(file_writer->format_ctx_out);
    if (ret) {
        printf("no trailer!\n");
    }
    avcodec_close(file_writer->video_ctx_out);
    
    if (!(file_writer->format_ctx_out->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&file_writer->format_ctx_out->pb);
    }
    
    avformat_free_context(file_writer->format_ctx_out);
    
    printf("File write done!\n");
    return 0;
}

int file_writer_push_video_frame(struct file_writer_t* file_writer,
                                 AVFrame* frame)
{
    int ret = av_buffersrc_add_frame_flags(file_writer->video_buffersrc_ctx,
                                           frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    AVFrame *filt_frame = av_frame_alloc();

    /* push the output frame into the filtergraph */
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR,
               "Error while feeding the filtergraph\n");
        goto end;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        ret = av_buffersink_get_frame(file_writer->video_buffersink_ctx,
                                      filt_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            goto end;
        }
        write_video_frame(file_writer, filt_frame);
        av_frame_unref(filt_frame);
    }
end:
    av_frame_free(&filt_frame);

    return ret;
}
