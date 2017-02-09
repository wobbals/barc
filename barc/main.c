//
//  main.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <assert.h>
#include <MagickWand/MagickWand.h>

#include "yuv_rgb.h"
#include "archive_stream.h"
#include "archive_package.h"
#include "magic_frame.h"
#include "audio_mixer.h"

const int out_width = 640;
const int out_height = 480;
const int out_pix_format = AV_PIX_FMT_YUV420P;
const int out_audio_format = AV_SAMPLE_FMT_FLTP;
const int src_audio_format = AV_SAMPLE_FMT_S16;
const int out_audio_num_channels = 1;

const char *video_filter_descr = "null";
const char *audio_filter_descr = "aresample=48000,aformat=sample_fmts=s16:channel_layouts=mono";

AVFilterContext *audio_buffersink_ctx;
AVFilterContext *audio_buffersrc_ctx;
AVFilterContext *video_buffersink_ctx;
AVFilterContext *video_buffersrc_ctx;
AVFilterGraph *video_filter_graph;
AVFilterGraph *audio_filter_graph;
const AVRational global_time_base = { 1, 1000 };
const int64_t out_video_fps = 30;
const int64_t out_sample_rate = 48000;
static AVCodec* video_codec_out;
static AVCodec* audio_codec_out;
static AVCodecContext* video_ctx_out;
static AVCodecContext* audio_ctx_out;
static AVFormatContext* format_ctx_out;
static AVStream* video_stream;
static AVStream* audio_stream;
static int64_t video_frame_ct;
static int64_t audio_frame_ct;
AVAudioFifo* audio_fifo;

static int init_audio_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    static const enum AVSampleFormat out_sample_fmts[] = { out_audio_format, -1 };
    static const int64_t out_channel_layouts[] = { AV_CH_LAYOUT_MONO, -1 };
    static const int out_sample_rates[] = { 48000, -1 };
    const AVFilterLink *outlink;
    AVRational time_base = { 1, out_sample_rate };

    audio_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !audio_filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!audio_ctx_out->channel_layout)
        audio_ctx_out->channel_layout = av_get_default_channel_layout(audio_ctx_out->channels);
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             time_base.num, time_base.den, audio_ctx_out->sample_rate,
             av_get_sample_fmt_name(audio_ctx_out->sample_fmt), audio_ctx_out->channel_layout);
    ret = avfilter_graph_create_filter(&audio_buffersrc_ctx, abuffersrc, "in",
                                       args, NULL, audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        goto end;
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&audio_buffersink_ctx, abuffersink, "out",
                                       NULL, NULL, audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(audio_buffersink_ctx, "sample_fmts", out_sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        goto end;
    }

    ret = av_opt_set_int_list(audio_buffersink_ctx, "channel_layouts", out_channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        goto end;
    }

    ret = av_opt_set_int_list(audio_buffersink_ctx, "sample_rates", out_sample_rates, -1,
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
    outputs->filter_ctx = audio_buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = audio_buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(audio_filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(audio_filter_graph, NULL)) < 0)
        goto end;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = audio_buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
           args);

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    
    return ret;
}


static int init_video_filters(const char *filters_descr)
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

    video_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !video_filter_graph) {
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

    ret = avfilter_graph_create_filter(&video_buffersrc_ctx, buffersrc, "in",
                                       args, NULL, video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&video_buffersink_ctx, buffersink, "out",
                                       NULL, NULL, video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(video_buffersink_ctx, "pix_fmts", pix_fmts,
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
    outputs->filter_ctx = video_buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = video_buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(video_filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(video_filter_graph, NULL)) < 0)
        goto end;

    printf("%s\n", avfilter_graph_dump(video_filter_graph, NULL));
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
    *resample_context = swr_alloc_set_opts(NULL,
                                           av_get_default_channel_layout(output_codec_context->channels),
                                           output_codec_context->sample_fmt,
                                           output_codec_context->sample_rate,
                                           av_get_default_channel_layout(input_codec_context->channels),
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
    assert(output_codec_context->sample_rate == input_codec_context->sample_rate);

    /** Open the resampler with the specified parameters. */
    if ((error = swr_init(*resample_context)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(resample_context);
        return error;
    }
    return 0;
}

int open_output_file(const char* filename)
{
    AVDictionary *opt = NULL;
    int ret;

    /* allocate the output media context */
    avformat_alloc_output_context2(&format_ctx_out, NULL, NULL, filename);
    if (!format_ctx_out) {
        printf("Could not deduce output format from file extension.\n");
        avformat_alloc_output_context2(&format_ctx_out, NULL, "mpeg", filename);
    }
    // fall back to mpeg
    if (!format_ctx_out) {
        printf("Could not allocate format output context");
        exit(1);
    }

    av_dump_format(format_ctx_out, 0, filename, 1);

    AVOutputFormat* fmt = format_ctx_out->oformat;

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&format_ctx_out->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            exit(1);
        }
    }

    /* find the video encoder */
    video_codec_out = avcodec_find_encoder(fmt->video_codec);
    if (!video_codec_out) {
        printf("Video codec not found\n");
        exit(1);
    }

    audio_codec_out = avcodec_find_encoder(fmt->audio_codec);
    if (!audio_codec_out) {
        printf("Audio codec not found\n");
        exit(1);
    }

    video_stream = avformat_new_stream(format_ctx_out, video_codec_out);
    audio_stream = avformat_new_stream(format_ctx_out, audio_codec_out);

    video_ctx_out = video_stream->codec;
    if (!video_ctx_out) {
        printf("Could not allocate video codec context\n");
        exit(1);
    }
    audio_ctx_out = audio_stream->codec;
    if (!audio_ctx_out) {
        printf("Could not allocate audio codec context\n");
        exit(1);
    }


    audio_stream->time_base.num = 1;
    audio_stream->time_base.den = out_sample_rate;

    // Codec configuration
    audio_ctx_out->bit_rate = 96000;
    audio_ctx_out->sample_fmt = out_audio_format;
    audio_ctx_out->sample_rate = out_sample_rate;
    audio_ctx_out->channels = 1;
    audio_ctx_out->channel_layout = AV_CH_LAYOUT_MONO;

    /* put sample parameters */
    video_ctx_out->qmin = 20;
    /* resolution must be a multiple of two */
    video_ctx_out->width = out_width;
    video_ctx_out->height = out_height;
    video_ctx_out->pix_fmt = out_pix_format;
    video_ctx_out->time_base = global_time_base;
    //video_ctx_out->max_b_frames = 1;

    if (fmt->video_codec == AV_CODEC_ID_H264) {
        av_opt_set(video_ctx_out->priv_data, "preset", "fast", 0);
    }

    /* Some formats want stream headers to be separate. */
    if (format_ctx_out->oformat->flags & AVFMT_GLOBALHEADER) {
        video_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;
        audio_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    /* open the context */
    if (avcodec_open2(video_ctx_out, video_codec_out, NULL) < 0) {
        printf("Could not open video codec\n");
        exit(1);
    }

    /* open the context */
    if (avcodec_open2(audio_ctx_out, audio_codec_out, NULL) < 0) {
        printf("Could not open audio codec\n");
        exit(1);
    }

    audio_fifo = av_audio_fifo_alloc(out_audio_format, out_audio_num_channels,
                                     audio_ctx_out->frame_size * 2);

    /* Write the stream header, if any. */
    ret = avformat_write_header(format_ctx_out, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        exit(1);
    }

    printf("Ready to encode video file %s\n", filename);

    return 0;
}

static int write_audio_frame(AVFrame* frame)
{
    int got_packet, ret;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the frame */
    ret = avcodec_encode_audio2(audio_ctx_out, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
        return 1;
    }

    if (got_packet) {
        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&pkt, audio_ctx_out->time_base,
                             audio_stream->time_base);
        pkt.stream_index = audio_stream->index;

        /* Write the compressed frame to the media file. */
        printf("Write audio frame %lld, size=%d pts=%lld duration=%lld\n",
               audio_frame_ct, pkt.size, pkt.pts, pkt.duration);
        audio_frame_ct++;
        ret = av_interleaved_write_frame(format_ctx_out, &pkt);

    } else {
        ret = 0;
    }
    av_free_packet(&pkt);
    return ret;
}

// inserts audio frame into filtergraph
static int push_audio_frame(AVFrame* frame) {
    int ret;
    AVFrame *filt_frame = av_frame_alloc();
    ret = av_buffersrc_add_frame_flags(audio_buffersrc_ctx, frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
    }

    /* pull filtered audio from the filtergraph */
    while (0 == ret) {
        ret = av_buffersink_get_frame(audio_buffersink_ctx, filt_frame);
        if (ret < 0) {
            break;
        }
        ret = write_audio_frame(filt_frame);
        av_frame_unref(filt_frame);
    }
    av_frame_free(&filt_frame);
    return ret;
}

static int write_video_frame(AVFrame* frame)
{
    int got_packet, ret;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(video_ctx_out, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
        exit(1);
    }

    if (got_packet) {
        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&pkt, global_time_base,
                             video_stream->time_base);
        pkt.stream_index = video_stream->index;

        /* Write the compressed frame to the media file. */
        printf("Write video frame %lld, size=%d pts=%lld\n",
               video_frame_ct, pkt.size, pkt.pts);
        video_frame_ct++;
        ret = av_interleaved_write_frame(format_ctx_out, &pkt);
    } else {
        ret = 0;
    }

    av_free_packet(&pkt);

    return ret;
}

void close_output_file()
{
    av_write_trailer(format_ctx_out);
    avcodec_close(video_ctx_out);

    if (!(format_ctx_out->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_ctx_out->pb);
    }

    avformat_free_context(format_ctx_out);

    printf("File write done!\n");

}

static int tick_audio(struct archive_t* archive, int64_t global_clock)
{
    int ret;

    // configure next audio frame to be encoded
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = audio_ctx_out->sample_fmt;
    output_frame->channel_layout = audio_ctx_out->channel_layout;
    output_frame->nb_samples = audio_ctx_out->frame_size;
    output_frame->pts = av_rescale_q(global_clock,
                                     global_time_base,
                                     audio_ctx_out->time_base);
    output_frame->sample_rate = out_sample_rate;

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

    // mix down samples
    audio_mixer_get_samples(archive, global_clock,
                            global_time_base, output_frame);

    // send it to the audio filter graph
    push_audio_frame(output_frame);

    return ret;
}

static int tick_video(struct archive_t* archive, int64_t global_clock)
{
    int ret;
    AVFrame* output_frame = av_frame_alloc();

    // Configure output frame buffer
    output_frame->format = AV_PIX_FMT_YUV420P;
    output_frame->width = out_width;
    output_frame->height = out_height;
    ret = av_frame_get_buffer(output_frame, 1);
    if (ret) {
        printf("No output AVFrame buffer to write video. Error: %s\n",
               av_err2str(ret));
        return ret;
    }

    AVFrame *filt_frame = av_frame_alloc();

    if (!output_frame || !filt_frame) {
        perror("Could not allocate frame");
        return -1;
    }

    archive_populate_stream_coords(archive, global_clock);

    struct archive_stream_t** active_streams;
    int active_stream_count;

    archive_get_active_streams_for_time(archive, global_clock,
                                        &active_streams,
                                        &active_stream_count);

    MagickWand* output_wand;
    magic_frame_start(&output_wand, out_width, out_height);

    printf("will write %d frames to magic\n", active_stream_count);
    // append source frames to magic frame
    for (int i = 0; i < active_stream_count; i++) {
        struct archive_stream_t* stream = active_streams[i];
        int64_t offset_pts;
        AVFrame* frame;
        archive_stream_peek_video(stream, &frame, &offset_pts);
        if (-1 == offset_pts) {
            continue;
        }

//        // compute how far off we are
//        int64_t delta = global_clock - offset_pts;
//        if (delta > abs(global_tick_time)) {
//            printf("Stream %d current offset vs global clock: %lld\n",
//                   i, delta);
//        }

        while (offset_pts != -1 && offset_pts < global_clock) {
            // pop frames until we catch up, hopefully not more than once.
            ret = archive_stream_pop_video(stream, &frame, &offset_pts);
            if (NULL != frame && !ret) {
                av_frame_free(&frame);
            }
        }

        // grab the next frame that hasn't been freed
        archive_stream_peek_video(stream, &frame, &offset_pts);

        if (frame) {
            magic_frame_add(output_wand,
                            frame,
                            *archive_stream_offset_x(stream),
                            *archive_stream_offset_y(stream),
                            *archive_stream_render_width(stream),
                            *archive_stream_render_height(stream));
        } else {
            printf("Warning: Ran out of frames on stream %d. "
                   "The time is %lld. Declared finish time is %lld\n",
                   i, global_clock, archive_stream_get_stop_offset(stream));
        }
    }

    ret = magic_frame_finish(output_wand, output_frame);

    if (!ret) {
        output_frame->pts = global_clock;

        ret = av_buffersrc_add_frame_flags(video_buffersrc_ctx, output_frame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF);
        /* push the output frame into the filtergraph */
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR,
                   "Error while feeding the filtergraph\n");
            goto end;
        }

        /* pull filtered frames from the filtergraph */
        while (1) {
            ret = av_buffersink_get_frame(video_buffersink_ctx, filt_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                goto end;

            write_video_frame(filt_frame);
            av_frame_unref(filt_frame);
        }
    }
end:
    av_frame_free(&filt_frame);
    av_frame_free(&output_frame);
    return ret;
}

void my_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    vprintf(fmt, vargs);
}

int main(int argc, char **argv)
{
    //av_log_set_level(AV_LOG_VERBOSE);
    //av_log_set_callback(my_log_callback);

    int ret;
    time_t start_time = time(NULL);
    av_register_all();
    avfilter_register_all();

    MagickWandGenesis();

    struct archive_t* archive;
    archive_open(&archive, out_width, out_height, "/Users/charley/src/barc/sample/product_meeting_sample");

    open_output_file("output.mp4");
    
    ret = init_audio_filters(audio_filter_descr);
    if (ret < 0)
    {
        printf("Error: init audio filters\n");
        exit(1);
    }

    ret = init_video_filters(video_filter_descr);
    if (ret < 0)
    {
        printf("Error: init video filters\n");
        exit(1);
    }


    float global_clock = 0;
    int64_t video_tick_time =
    global_time_base.den / out_video_fps / global_time_base.num;
    float audio_tick_time =
    (float)audio_ctx_out->frame_size * (float) global_time_base.den /
    (float)audio_ctx_out->sample_rate;
    int64_t last_audio_time = 0;
    int64_t last_video_time = 0;
    char need_video;
    char need_audio;

    int64_t archive_finish_time = archive_get_finish_clock_time(archive);

    /* kick off the global clock and begin composing */
    while (archive_finish_time >= global_clock) {
        need_audio = (global_clock - last_audio_time) >= audio_tick_time;
        need_video = (global_clock - last_video_time) >= video_tick_time;

        // skip this tick if there are no frames need rendering
        if (!need_audio && !need_video) {
            global_clock++;
            continue;
        }

        printf("global_clock: %f need_audio:%d need_video:%d\n",
               global_clock, need_audio, need_video);

        if (need_audio) {
            last_audio_time = global_clock;
            tick_audio(archive, global_clock);
        }

        if (need_video) {
            last_video_time = global_clock;
            tick_video(archive, global_clock);
        }

        global_clock++;
    }
end:
    avfilter_graph_free(&video_filter_graph);
    //av_frame_free(&filt_frame);
    //archive_stream_free(&archive_streams[0]);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        //exit(1);
    }
    MagickWandTerminus();

    close_output_file();

    time_t finish_time = time(NULL);

    printf("Composition took %ld seconds\n", finish_time - start_time);

    char cwd[1024];
    printf("%s\n", getcwd(cwd, sizeof(cwd)));
    
    exit(0);
}
