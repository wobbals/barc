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
#include <MagickWand/MagickWand.h>

#include "yuv_rgb.h"
#include "archive_stream.h"


//const char *filter_descr = "colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131";
const char *filter_descr = "scale=960:720";
//const char *filter_descr = "null";
//const char *filter_descr = "fps=fps=30";
/* other way:
 scale=78:24 [scl]; [scl] transpose=cclock // assumes "[in]" and "[out]" to be input output pads respectively
 */

AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
struct archive_stream_t archive_stream = { 0 };

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = archive_stream.format_context->streams[archive_stream.video_stream_index]->time_base;
    //AVRational time_base = dec_ctx->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             archive_stream.decode_context->width,
             archive_stream.decode_context->height,
             archive_stream.decode_context->pix_fmt,
             time_base.num, time_base.den,
             archive_stream.decode_context->sample_aspect_ratio.num,
             archive_stream.decode_context->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
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
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    printf("%s\n", avfilter_graph_dump(filter_graph, NULL));
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static AVCodec* video_codec_out;
static AVCodecContext* video_ctx_out;
static AVFormatContext* format_ctx_out;
static AVStream* video_stream;
static int video_frame_ct;

int open_output_file(const char* filename)
{
    AVDictionary *opt = NULL;
    int ret;

    /* allocate the output media context */
    avformat_alloc_output_context2(&format_ctx_out, NULL, NULL, filename);
    if (!format_ctx_out) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&format_ctx_out, NULL, "mpeg", filename);
    }
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

    // Add codecs
    int codec_id = fmt->video_codec;

    /* find the video encoder */
    video_codec_out = avcodec_find_encoder(codec_id);
    if (!video_codec_out) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    video_stream = avformat_new_stream(format_ctx_out, video_codec_out);

    video_ctx_out = video_stream->codec;
    if (!video_ctx_out) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }
    // Codec configuration

    /* put sample parameters */
    video_ctx_out->bit_rate = 500000;
    /* resolution must be a multiple of two */
    video_ctx_out->width = 960;
    video_ctx_out->height = 720;

    /* frames per second */
    //video_stream->time_base = video_ctx_out->time_base = (AVRational){1,25};
    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    // overwriting the keyframe interval just causes complaining.
    //video_ctx_out->gop_size = 25;
    video_ctx_out->max_b_frames = 1;
    video_ctx_out->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec_id == AV_CODEC_ID_H264) {
        av_opt_set(video_ctx_out->priv_data, "preset", "fast", 0);
    }

    /* Some formats want stream headers to be separate. */
    if (format_ctx_out->oformat->flags & AVFMT_GLOBALHEADER)
        video_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open the context */
    if (avcodec_open2(video_ctx_out, video_codec_out, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

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
        av_packet_rescale_ts(&pkt, archive_stream.decode_context->time_base,
                             video_stream->time_base);
        pkt.stream_index = video_stream->index;

        /* Write the compressed frame to the media file. */
        printf("Write frame %d, size=%d pts=%lld\n", video_frame_ct++, pkt.size,
               pkt.pts);
        return av_interleaved_write_frame(format_ctx_out, &pkt);

    } else {
        ret = 0;
    }
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

#define QuantumScale  ((MagickRealType) 1.0/(MagickRealType) QuantumRange)
#define SigmoidalContrast(x) \
(QuantumRange*(1.0/(1+exp(10.0*(0.5-QuantumScale*x)))-0.0066928509)*1.0092503)

#define ThrowWandException(wand) \
{ \
char \
*description; \
\
ExceptionType \
severity; \
\
description=MagickGetException(wand,&severity); \
(void) fprintf(stderr,"%s %s %lu %s\n",GetMagickModule(),description); \
description=(char *) MagickRelinquishMemory(description); \
exit(-1); \
}

int imageMagick(AVFrame* frame) {

    MagickBooleanType
    status;

    PixelInfo
    pixelInfo;

    MagickWand
    *contrast_wand,
    *image_wand;

    PixelIterator
    *contrast_iterator,
    *iterator;

    PixelWand
    **contrast_pixels,
    **pixels;

    register ssize_t
    x;

    size_t
    width;

    ssize_t
    y;

    int rgb_bytes_per_pixel = 3; // 4 for rgba
    uint8_t* rgb_buf = malloc(rgb_bytes_per_pixel * frame->height * frame->width);
    yuv420_rgb24_sseu(frame->width, frame->height,
                      frame->data[0], frame->data[1], frame->data[2],
                      frame->linesize[0], frame->linesize[1],
                      rgb_buf, frame->width * 3,
                      YCBCR_709);

    /*
     Read an image.
     */
    MagickWandGenesis();
    image_wand=NewMagickWand();
    status=MagickConstituteImage(image_wand, frame->width, frame->height,"RGB",
                                 CharPixel, rgb_buf);
    if (status == MagickFalse)
        ThrowWandException(image_wand);
    contrast_wand=CloneMagickWand(image_wand);
    /*
     Sigmoidal non-linearity contrast control.
     */
    iterator=NewPixelIterator(image_wand);
    contrast_iterator=NewPixelIterator(contrast_wand);
    if ((iterator == (PixelIterator *) NULL) ||
        (contrast_iterator == (PixelIterator *) NULL))
        ThrowWandException(image_wand);
    for (y=0; y < (ssize_t) MagickGetImageHeight(image_wand); y++)
    {
        pixels=PixelGetNextIteratorRow(iterator,&width);
        contrast_pixels=PixelGetNextIteratorRow(contrast_iterator,&width);
        if ((pixels == (PixelWand **) NULL) ||
            (contrast_pixels == (PixelWand **) NULL))
            break;
        for (x=0; x < (ssize_t) width; x++)
        {
            PixelGetMagickColor(pixels[x],&pixelInfo);
            pixelInfo.red=SigmoidalContrast(pixelInfo.red);
            pixelInfo.green=SigmoidalContrast(pixelInfo.green);
            pixelInfo.blue=SigmoidalContrast(pixelInfo.blue);
            pixelInfo.index=SigmoidalContrast(pixelInfo.index);
            PixelSetPixelColor(contrast_pixels[x],&pixelInfo);
        }
        (void) PixelSyncIterator(contrast_iterator);
    }
    if (y < (ssize_t) MagickGetImageHeight(image_wand))
        ThrowWandException(image_wand);
    contrast_iterator=DestroyPixelIterator(contrast_iterator);
    iterator=DestroyPixelIterator(iterator);
    image_wand=DestroyMagickWand(image_wand);
    MagickExportImagePixels(contrast_wand, 0, 0, frame->width, frame->height,
                            "RGB", CharPixel, rgb_buf);
    /*
     Write the image then destroy it.
     */
    // send contrast_wand off to the frame buffer
    rgb24_yuv420_sseu(frame->width, frame->height,
                     rgb_buf, frame->width * rgb_bytes_per_pixel,
                     frame->data[0], frame->data[1], frame->data[2],
                     frame->linesize[0], frame->linesize[1], YCBCR_709);
    free(rgb_buf);
    //status=MagickWriteImages(contrast_wand,argv[2],MagickTrue);
    //if (status == MagickFalse)
    //    ThrowWandException(image_wand);
    contrast_wand=DestroyMagickWand(contrast_wand);
    MagickWandTerminus();
    return(0);
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
    AVPacket packet;
    AVFrame *frame;
    int64_t offset_pts;
    AVFrame *filt_frame = av_frame_alloc();
    int got_frame;

    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        exit(1);
    }

    av_register_all();
    avfilter_register_all();

    if ((ret = archive_stream_open(&archive_stream, argv[1])) < 0)
        goto end;
    if ((ret = init_filters(filter_descr)) < 0)
        goto end;

    open_output_file("output.mp4");

    /* read all packets */
    while (1) {

        archive_stream_pop_video_frame(&archive_stream, &frame, &offset_pts);

        if (frame) {
            //imageMagick(frame);
            frame->pts = av_frame_get_best_effort_timestamp(frame);

            /* push the decoded frame into the filtergraph */
            if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                break;
            }

            /* pull filtered frames from the filtergraph */
            while (1) {
                ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0)
                    goto end;

                write_video_frame(filt_frame);
                av_frame_unref(filt_frame);
            }
            av_frame_unref(frame);
        } else {
            printf("No more frames from archive stream\n");
            ret = 0;
            break;
        }
    }
end:
    avfilter_graph_free(&filter_graph);
    av_frame_free(&frame);
    //av_frame_free(&filt_frame);
    archive_stream_free(&archive_stream);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        //exit(1);
    }

    close_output_file();

    char cwd[1024];
    printf("%s\n", getcwd(cwd, sizeof(cwd)));
    
    exit(0);
}
