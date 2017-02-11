//
//  main.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include <unistd.h>
#include <libavformat/avformat.h>
#include <assert.h>
#include <MagickWand/MagickWand.h>
#include <uv.h>

#include "yuv_rgb.h"
#include "archive_stream.h"
#include "archive_package.h"
#include "audio_mixer.h"
#include "file_writer.h"
#include "zipper.h"
#include "frame_builder.h"

const int out_width = 640;
const int out_height = 480;

static int tick_audio(struct file_writer_t* file_writer,
                      struct archive_t* archive, int64_t global_clock,
                      AVRational global_time_base)
{
    int ret;

    // configure next audio frame to be encoded
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = file_writer->audio_ctx_out->sample_fmt;
    output_frame->channel_layout = file_writer->audio_ctx_out->channel_layout;
    output_frame->nb_samples = file_writer->audio_ctx_out->frame_size;
    output_frame->pts = av_rescale_q(global_clock,
                                     global_time_base,
                                     file_writer->audio_ctx_out->time_base);
    output_frame->sample_rate = file_writer->audio_ctx_out->sample_rate;

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
    file_writer_push_audio_frame(file_writer, output_frame);

    return ret;
}

struct frame_builder_callback_data_t {
    int64_t clock_time;
    struct file_writer_t* file_writer;
};

static void frame_builder_cb(AVFrame* frame, void *p) {
    struct frame_builder_callback_data_t* data =
    ((struct frame_builder_callback_data_t*)p);
    int64_t clock_time = data->clock_time;
    struct file_writer_t* file_writer = data->file_writer;

    frame->pts = clock_time;
    int ret = file_writer_push_video_frame(file_writer, frame);
    if (ret) {
        printf("Unable to push video frame %lld\n", frame->pts);
    }
    free(p);
}

static int tick_video(struct file_writer_t* file_writer,
                      struct frame_builder_t* frame_builder,
                      struct archive_t* archive, int64_t clock_time,
                      AVRational clock_time_base)
{
    int ret = -1;

    archive_populate_stream_coords(archive, clock_time, clock_time_base);

    struct archive_stream_t** active_streams;
    int active_stream_count;

    archive_get_active_streams_for_time(archive, clock_time,
                                        &active_streams,
                                        &active_stream_count);

    struct frame_builder_callback_data_t* callback_data =
    (struct frame_builder_callback_data_t*)
    malloc(sizeof(struct frame_builder_callback_data_t));
    callback_data->clock_time = clock_time;
    callback_data->file_writer = file_writer;
    frame_builder_begin_frame(frame_builder, out_width, out_height,
                              (enum AVPixelFormat)AV_PIX_FMT_YUV420P,
                              callback_data);

    // append source frames to magic frame
    for (int i = 0; i < active_stream_count; i++) {
        struct archive_stream_t* stream = active_streams[i];
        if (!archive_stream_has_video_for_time(stream, clock_time,
                                               clock_time_base))
        {
            continue;
        }

        struct smart_frame_t* smart_frame;
        ret = archive_stream_get_video_for_time(stream, &smart_frame,
                                                clock_time, clock_time_base);
        if (NULL == smart_frame || ret) {
            continue;
        }

        struct frame_builder_subframe_t subframe;
        subframe.smart_frame = smart_frame;
        subframe.x_offset = archive_stream_get_offset_x(stream);
        subframe.y_offset = archive_stream_get_offset_y(stream);
        subframe.render_width = archive_stream_get_render_width(stream);
        subframe.render_height = archive_stream_get_render_height(stream);
        
        frame_builder_add_subframe(frame_builder, &subframe);

    }

    frame_builder_finish_frame(frame_builder, frame_builder_cb);
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

    char* path;
    if (1 >= argc) {
        path = "/Users/charley/src/barc/sample/audio_sync.zip";
    } else {
        path = argv[1];
    }

    struct stat file_stat;
    stat(path, &file_stat);

    if (S_ISDIR(file_stat.st_mode)) {
        printf("using directory %s\n", path);
    } else if (S_ISREG(file_stat.st_mode)) {
        printf("attempt to unzip regular file %s\n", path);
        path = unzip_archive(path);
    } else {
        printf("Unknown file type %s\n", path);
        exit(-1);
    }

    if (!path) {
        printf("No working path. Exit.\n");
        exit(-1);
    }

    struct archive_t* archive;
    archive_open(&archive, out_width, out_height, path);

    struct file_writer_t* file_writer;
    file_writer_alloc(&file_writer);
    file_writer_open(file_writer, "output.mp4", out_width, out_height);

    struct frame_builder_t* frame_builder;
    frame_builder_alloc(&frame_builder);

    AVRational global_time_base = {1, 1000};
    // todo: fix video_fps to coordinate with the file writer
    float out_video_fps = 30;
    archive_set_output_video_fps(archive, out_video_fps);
    float global_clock = 0;
    int64_t video_tick_time =
    global_time_base.den / out_video_fps / global_time_base.num;
    float audio_tick_time =
    (float)file_writer->audio_ctx_out->frame_size *
    (float) global_time_base.den /
    (float)file_writer->audio_ctx_out->sample_rate;
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
            tick_audio(file_writer, archive, global_clock, global_time_base);
        }

        if (need_video) {
            last_video_time = global_clock;
            tick_video(file_writer, frame_builder, archive,
                       global_clock, global_time_base);
        }

        global_clock++;
    }
end:
    //av_frame_free(&filt_frame);
    //archive_stream_free(&archive_streams[0]);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        //exit(1);
    }
    // wait for all frames to write out before closing down.
    frame_builder_join(frame_builder);
    frame_builder_free(frame_builder);
    MagickWandTerminus();

    file_writer_close(file_writer);
    file_writer_free(file_writer);

    time_t finish_time = time(NULL);

    printf("Composition took %ld seconds\n", finish_time - start_time);

    char cwd[1024];
    printf("%s\n", getcwd(cwd, sizeof(cwd)));
    
    return(0);
}
