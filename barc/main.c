//
//  main.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include <unistd.h>
#include <ctype.h>
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

static int tick_audio(struct file_writer_t* file_writer,
                      struct archive_t* archive, int64_t clock_time,
                      AVRational clock_time_base,
                      int64_t clock_begin_offset, char skip_frame)
{
    int ret;

    // configure next audio frame to be encoded
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = file_writer->audio_ctx_out->sample_fmt;
    output_frame->channel_layout = file_writer->audio_ctx_out->channel_layout;
    output_frame->nb_samples = file_writer->audio_ctx_out->frame_size;
    // output pts is offset back to zero for late starts (see -b option)
    output_frame->pts = av_rescale_q(clock_time - clock_begin_offset,
                                     clock_time_base,
                                     file_writer->audio_ctx_out->time_base);
    output_frame->sample_rate = file_writer->audio_ctx_out->sample_rate;
    // adjust to local time in audio scale units, without begin offset
    int64_t local_source_ts =
    av_rescale_q(clock_time,
                 clock_time_base,
                 file_writer->audio_ctx_out->time_base);

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

    // mix down samples using original time
    audio_mixer_get_samples(archive,
                            local_source_ts,
                            file_writer->audio_ctx_out->time_base,
                            output_frame);

    if (!skip_frame) {
        // send it to the audio filter graph
        file_writer_push_audio_frame(file_writer, output_frame);
    }
    return ret;
}

struct frame_builder_callback_data_t {
    int64_t clock_time;
    int64_t clock_begin_offset;
    struct file_writer_t* file_writer;
};

static void frame_builder_cb(AVFrame* frame, void *p) {
    struct frame_builder_callback_data_t* data =
    ((struct frame_builder_callback_data_t*)p);
    int64_t clock_time = data->clock_time;
    int64_t clock_begin_offset = data->clock_begin_offset;
    struct file_writer_t* file_writer = data->file_writer;

    frame->pts = clock_time - clock_begin_offset;
    int ret = file_writer_push_video_frame(file_writer, frame);
    if (ret) {
        printf("Unable to push video frame %lld\n", frame->pts);
    }
    free(p);
}

static int tick_video(struct file_writer_t* file_writer,
                      struct frame_builder_t* frame_builder,
                      struct archive_t* archive, int64_t clock_time,
                      AVRational clock_time_base,
                      int64_t clock_begin_offset, char skip_frame)
{
    int ret = -1;

    archive_populate_stream_coords(archive, clock_time, clock_time_base);

    struct archive_stream_t** active_streams;
    int active_stream_count;

    archive_get_active_streams_for_time(archive, clock_time, clock_time_base,
                                        &active_streams,
                                        &active_stream_count);
    // if we're skipping the frame, no need for a magic frame. just pull the
    // frames out of the video graph and do nothing.
    if (!skip_frame) {
        struct frame_builder_callback_data_t* callback_data =
        (struct frame_builder_callback_data_t*)
        malloc(sizeof(struct frame_builder_callback_data_t));
        callback_data->clock_time = clock_time;
        callback_data->clock_begin_offset = clock_begin_offset;
        callback_data->file_writer = file_writer;
        frame_builder_begin_frame(frame_builder,
                                  file_writer->out_width,
                                  file_writer->out_height,
                                  (enum AVPixelFormat)AV_PIX_FMT_YUV420P,
                                  callback_data);
    }

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

        if (!skip_frame) {
            frame_builder_add_subframe(frame_builder, &subframe);
        }
    }
    if (!skip_frame) {
        frame_builder_finish_frame(frame_builder, frame_builder_cb);
    }
    return ret;
}

void my_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    vprintf(fmt, vargs);
}

int main(int argc, char **argv)
{
    char* input_path = NULL;
    char* output_path = NULL;
    char* css_preset = NULL;
    char* css_custom = NULL;
    int out_width = 0;
    int out_height = 0;
    int64_t begin_offset = -1;
    int64_t end_offset = -1;
    int c;

    while ((c = getopt (argc, argv, "i:o:w:h:p:c:b:e:")) != -1) {
        switch (c)
        {
            case 'i':
                input_path = optarg;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 'w':
                out_width = atoi(optarg);
                break;
            case 'h':
                out_height = atoi(optarg);
                break;
            case 'b':
                begin_offset = atoi(optarg);
                break;
            case 'e':
                end_offset = atoi(optarg);
                break;
            case 'p':
                css_preset = optarg;
                break;
            case 'c':
                css_custom = optarg;
                break;
            case '?':
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character `\\x%x'.\n",
                             optopt);
                return 1;
            default:
                abort ();
        }
    }

    if (!input_path) {
        input_path = "/Users/charley/src/barc/sample/audio_sync.zip";
    }
    if (!output_path) {
        output_path = "output.mp4";
    }
    if (!out_width) {
        out_width = 640;
    }
    if (!out_height) {
        out_height = 480;
    }

    //av_log_set_level(AV_LOG_VERBOSE);
    //av_log_set_callback(my_log_callback);

    int ret;
    time_t start_time = time(NULL);
    av_register_all();
    avfilter_register_all();

    MagickWandGenesis();

    struct stat file_stat;
    stat(input_path, &file_stat);

    if (S_ISDIR(file_stat.st_mode)) {
        printf("using directory %s\n", input_path);
    } else if (S_ISREG(file_stat.st_mode)) {
        printf("attempt to unzip regular file %s\n", input_path);
        input_path = unzip_archive(input_path);
    } else {
        printf("Unknown file type %s\n", input_path);
        exit(-1);
    }

    if (!input_path) {
        printf("No working path. Exit.\n");
        exit(-1);
    }

    struct archive_t* archive;
    archive_open(&archive, out_width, out_height, input_path,
                 css_preset, css_custom);

    struct file_writer_t* file_writer;
    file_writer_alloc(&file_writer);
    file_writer_open(file_writer, output_path, out_width, out_height);

    struct frame_builder_t* frame_builder;
    frame_builder_alloc(&frame_builder);

    AVRational global_time_base = {1, 1000};
    // todo: fix video_fps to coordinate with the file writer
    double out_video_fps = 30;
    archive_set_output_video_fps(archive, out_video_fps);
    double global_clock = 0.;
    int64_t video_tick_time =
    global_time_base.den / out_video_fps / global_time_base.num;
    double audio_tick_time =
    (double)file_writer->audio_ctx_out->frame_size *
    (double) global_time_base.den /
    (double)file_writer->audio_ctx_out->sample_rate;
    double next_clock;

    int64_t archive_finish_time = archive_get_finish_clock_time(archive);

    double next_clock_times[2];
    next_clock_times[0] = audio_tick_time;
    next_clock_times[1] = video_tick_time;
    char need_track[2];
    need_track[0] = 1;
    need_track[1] = 1;
    char skip_frame;

    if (begin_offset < 0) {
        begin_offset = 0;
    } else {
        begin_offset *= global_time_base.den;
        begin_offset /= global_time_base.num;
    }

    // truncate archive_finish_time if an end_offset has been set and it's less
    // than the finish time declared in manifest
    if (end_offset > 0) {
        end_offset *= global_time_base.den;
        end_offset /= global_time_base.num;
        archive_finish_time = fmin(archive_finish_time, end_offset);
    }

    /* kick off the global clock and begin composing */
    while (archive_finish_time >= global_clock) {
        skip_frame = (global_clock < begin_offset);

        printf("{\"progress\": {\"complete\": %lld, \"total\": %lld }}\n",
               (int64_t)global_clock, archive_finish_time);
        if (skip_frame) {
            printf("skipping frame\n");
        } else {
            printf("need_audio:%d need_video:%d\n", need_track[0], need_track[1]);
        }

        // process audio and video tracks, as needed
        if (need_track[0]) {
            tick_audio(file_writer, archive, global_clock, global_time_base,
                       begin_offset, skip_frame);
            next_clock_times[0] = global_clock + audio_tick_time;
        }

        if (need_track[1]) {
            tick_video(file_writer, frame_builder, archive,
                       global_clock, global_time_base,
                       begin_offset, skip_frame);
            next_clock_times[1] = global_clock + video_tick_time;
        }

        // calculate exactly when we need to wake up again.
        // first, assume audio is next.
        next_clock = next_clock_times[0];
        if (fabs(next_clock - next_clock_times[1]) < 0.0001) {
            // if we land on a common factor of both track intervals, floating
            // point math might not make a perfect match between the two
            // floating timestamps. grab both tracks on the next tick
            need_track[1] = 1;
            need_track[0] = 1;
        } else if (next_clock > next_clock_times[1]) {
            // otherwise, check to see if audio is indeed the next track
            next_clock = next_clock_times[1];
            need_track[1] = 1;
            need_track[0] = 0;
        } else {
            // take only what we need
            need_track[0] = 1;
            need_track[1] = 0;
        }

        global_clock = next_clock;
    }
end:
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        //exit(1);
    }

    printf("Waiting for frame builder queue to finish...");
    // wait for all frames to write out before closing down.
    frame_builder_wait(frame_builder, 0);
    frame_builder_free(frame_builder);
    printf("..done!\n");

    MagickWandTerminus();

    printf("Close file writer..");
    file_writer_close(file_writer);
    file_writer_free(file_writer);
    printf("..done!\n");

    time_t finish_time = time(NULL);

    printf("Composition took %ld seconds\n", finish_time - start_time);

    char cwd[1024];
    printf("%s\n", getcwd(cwd, sizeof(cwd)));
    
    return(0);
}
