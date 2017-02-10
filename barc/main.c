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
#include <libswresample/swresample.h>
#include <assert.h>
#include <MagickWand/MagickWand.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ftw.h>
#include <stdio.h>
#include <fcntl.h>
#include <zip.h>

#include "yuv_rgb.h"
#include "archive_stream.h"
#include "archive_package.h"
#include "magic_frame.h"
#include "audio_mixer.h"
#include "file_writer.h"

const int out_width = 640;
const int out_height = 360;

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

static int tick_video(struct file_writer_t* file_writer,
                      struct archive_t* archive, int64_t global_clock)
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


    if (!output_frame) {
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
        ret = file_writer_push_video_frame(file_writer, output_frame);
    }
end:
    av_frame_free(&output_frame);
    return ret;
}

void my_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    vprintf(fmt, vargs);
}

int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);

    if (rv)
        perror(fpath);

    return rv;
}

int rmrf(const char *path)
{
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static void safe_create_dir(const char *dir)
{
    if (mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
        if (errno != EEXIST) {
            perror(dir);
            exit(1);
        }
    }
}

char* unzip_archive(const char* path) {
    int err, i, fd;
    int64_t sum, len;
    struct zip_file *zf;
    struct zip_stat sb;
    char buf[100];
    struct zip* za = zip_open(path, ZIP_CHECKCONS | ZIP_RDONLY, &err);
    if (NULL == za) {
        zip_error_to_str(buf, sizeof(buf), err, errno);
        printf("can't open zip archive `%s': %s/n",
                path, buf);
        return NULL;
    }

    const char* working_directory = "out";
    rmrf(working_directory);
    safe_create_dir(working_directory);
    err = chdir(working_directory);

    for (i = 0; i < zip_get_num_entries(za, 0); i++) {
        if (zip_stat_index(za, i, 0, &sb) == 0) {
            printf("==================\n");
            len = strlen(sb.name);
            printf("Name: [%s], ", sb.name);
            printf("Size: [%llu], ", sb.size);
            printf("mtime: [%u]\n", (unsigned int)sb.mtime);
            if (sb.name[len - 1] == '/') {
                safe_create_dir(sb.name);
            } else {
                zf = zip_fopen_index(za, i, 0);
                if (!zf) {
                    fprintf(stderr, "boese, boese\n");
                    exit(100);
                }

                fd = open(sb.name, O_RDWR | O_TRUNC | O_CREAT, 0644);
                if (fd < 0) {
                    fprintf(stderr, "boese, boese\n");
                    exit(101);
                }

                sum = 0;
                while (sum != sb.size) {
                    len = zip_fread(zf, buf, 100);
                    if (len < 0) {
                        fprintf(stderr, "boese, boese\n");
                        exit(102);
                    }
                    write(fd, buf, len);
                    sum += len;
                }
                close(fd);
                zip_fclose(zf);
            }
        } else {
            printf("File[%s] Line[%d]/n", __FILE__, __LINE__);
        }
    }
    char* pwd = getcwd(NULL, 0);
    return pwd;
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
        path = "/Users/charley/src/barc/sample/allhands_sample.zip";
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

    AVRational global_time_base = {1, 1000};
    // todo: fix video_fps to coordinate with the file writer
    float out_video_fps = 30;
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
            tick_video(file_writer, archive, global_clock);
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
    MagickWandTerminus();

    file_writer_close(file_writer);
    file_writer_free(file_writer);

    time_t finish_time = time(NULL);

    printf("Composition took %ld seconds\n", finish_time - start_time);

    char cwd[1024];
    printf("%s\n", getcwd(cwd, sizeof(cwd)));
    
    exit(0);
}
