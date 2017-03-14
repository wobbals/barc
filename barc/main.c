//
//  main.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

#include <unistd.h>
#include <ctype.h>
#include <libavformat/avformat.h>
#include <assert.h>
#include <MagickWand/MagickWand.h>
#include <uv.h>
#include <getopt.h>

#include "yuv_rgb.h"
#include "archive_stream.h"
#include "archive_package.h"
#include "audio_mixer.h"
#include "file_writer.h"
#include "zipper.h"
#include "frame_builder.h"

int main(int argc, char **argv)
{
    char* input_path = NULL;
    char* output_path = NULL;
    char* css_preset = NULL;
    char* css_custom = NULL;
    char* manifest_supplemental = NULL;
    int out_width = 0;
    int out_height = 0;
    int64_t begin_offset = -1;
    int64_t end_offset = -1;
    int c;

    static struct option long_options[] =
    {
        /* These options set a flag. */
        //{"repairmode", no_argument, &repairmode_flag, 0},
        /* These options don’t set a flag.
         We distinguish them by their indices. */
        {"input", required_argument,        0, 'i'},
        {"output", optional_argument,       0, 'o'},
        {"width", optional_argument,        0, 'w'},
        {"height", optional_argument,       0, 'h'},
        {"css_preset", optional_argument,   0, 'p'},
        {"custom_css", optional_argument,   0, 'c'},
        {"css_preset", optional_argument,   0, 'p'},
        {"begin_offset", optional_argument, 0, 'b'},
        {"end_offset", optional_argument,   0, 'e'},
        {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "i:o:w:h:p:c:b:e:",
                            long_options, &option_index)) != -1)
    {
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
                 css_preset, css_custom, manifest_supplemental);

    struct file_writer_t* file_writer;
    file_writer_alloc(&file_writer);
    file_writer_open(file_writer, output_path, out_width, out_height);

    struct frame_builder_t* frame_builder;
    frame_builder_alloc(&frame_builder);

    AVRational global_time_base = {1, 1000};
    // todo: fix video_fps to coordinate with the file writer
    double out_video_fps = 30;
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
        // TODO: not this. add seek support to audio mixer
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
