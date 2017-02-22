//
//  frame_builder.c
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

extern "C" {
#include <stdlib.h>
#include <MagickWand/MagickWand.h>
#include <uv.h>
#include <unistd.h>

#include "frame_builder.h"
#include "magic_frame.h"
}

#include <vector>
#include <map>

// Workaround C++ issue with ffmpeg macro
#ifndef __clang__
#undef av_err2str
#define av_err2str(errnum) \
av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

static int job_counter;
static void crunch_frame(uv_work_t* work);
static void after_crunch_frame(uv_work_t* work, int status);
static void free_job(struct frame_job_t* job);
static void frame_builder_worker(void* p);

struct frame_job_t {
    uv_work_t request;
    int width;
    int height;
    enum AVPixelFormat format;
    std::vector<struct frame_builder_subframe_t*> subframes;
    frame_builder_cb_t callback;
    AVFrame* output_frame;
    frame_builder_t* builder;
    int serial_number;
    void* p;
};

struct frame_builder_t {
    struct frame_job_t* current_job;
    uv_mutex_t job_queue_lock;
    int max_queue_size;
    std::map<int, struct frame_job_t*>pending_jobs;
    std::map<int, struct frame_job_t*>finished_jobs;
    char running;
    uv_loop_t *loop;
    uv_thread_t loop_thread;
    int finish_serial;
};

int frame_builder_alloc(struct frame_builder_t** frame_builder) {
    struct frame_builder_t* result = (struct frame_builder_t*)
    calloc(1, sizeof(struct frame_builder_t));
    result->pending_jobs = std::map<int, struct frame_job_t*>();
    result->finished_jobs = std::map<int, struct frame_job_t*>();
    result->loop = (uv_loop_t*) malloc(sizeof(uv_loop_t));
    result->running = 1;
    result->finish_serial = 0;
    // shape the thread pool a bit
    uv_cpu_info_t* cpu_infos;
    int cpu_count;
    uv_cpu_info(&cpu_infos, &cpu_count);
    cpu_count = fmax(1, cpu_count - 2);
    char str[4];
    sprintf(str, "%d", cpu_count);
    uv_free_cpu_info(cpu_infos, cpu_count);
    // ...or, don't. your mileage may vary. see what works for you.
    setenv("UV_THREADPOOL_SIZE", str, 0);
    uv_loop_init(result->loop);
    uv_thread_create(&result->loop_thread, frame_builder_worker, result);

    // configure job queue size. bigger queue uses more memory, but not
    // necessarily improves performance.
    // this should be at least as big as the thread pool size, just to give
    // all the available resources something to do.
    result->max_queue_size = 64;
    uv_mutex_init(&result->job_queue_lock);

    *frame_builder = result;
    return 0;
}

void frame_builder_free(struct frame_builder_t* frame_builder) {
    int ret;
    frame_builder->running = 0;
    uv_stop(frame_builder->loop);
    do {
        ret = uv_loop_close(frame_builder->loop);
    } while (UV_EBUSY == ret);
    uv_thread_join(&frame_builder->loop_thread);
    uv_mutex_destroy(&frame_builder->job_queue_lock);
    free(frame_builder);
}

int frame_builder_begin_frame(struct frame_builder_t* frame_builder,
                              int width, int height,
                              enum AVPixelFormat format, void* p)
{
    struct frame_job_t* job = (struct frame_job_t*)
    calloc(1, sizeof(struct frame_job_t));
    job->builder = frame_builder;
    job->serial_number = job_counter++;
    job->width = width;
    job->height = height;
    job->format = format;
    job->p = p;
    frame_builder->current_job = job;
    return 0;
}

int frame_builder_add_subframe(struct frame_builder_t* frame_builder,
                               struct frame_builder_subframe_t* subframe)
{
    struct frame_builder_subframe_t* subframe_copy =
    (struct frame_builder_subframe_t*)
    calloc(1, sizeof(struct frame_builder_subframe_t));
    memcpy(subframe_copy, subframe, sizeof(struct frame_builder_subframe_t));
    smart_frame_retain(subframe->smart_frame);
    frame_builder->current_job->subframes.push_back(subframe_copy);
    return 0;
}

int frame_builder_finish_frame(struct frame_builder_t* frame_builder,
                               frame_builder_cb_t callback) {
    struct frame_job_t* job = frame_builder->current_job;
    job->callback = callback;
    job->request.data = job;
    int ret = 0;

    size_t current_queue_size = frame_builder->pending_jobs.size();
    if (current_queue_size > frame_builder->max_queue_size) {
        printf("Queue size exceeded. Drain half before proceeding.\n");
        frame_builder_wait(frame_builder, (int)current_queue_size / 2);
    }

    uv_mutex_lock(&frame_builder->job_queue_lock);
    frame_builder->pending_jobs[job->serial_number] = job;
    uv_mutex_unlock(&frame_builder->job_queue_lock);
    // release the lock before doing anything crazy
    printf("Schedule job %d. Pending queue size: %lu\n",
           job->serial_number,
           current_queue_size);

    // This debug env var won't kill all threads, just the ones we create to
    // offload magic frame generation.
    if (getenv("BARC_DISABLE_MULTITHREADING")) {
        crunch_frame(&job->request);
        after_crunch_frame(&job->request, 0);
    } else {
        ret = uv_queue_work(frame_builder->loop,
                            &(job->request),
                            crunch_frame,
                            after_crunch_frame);
    }
    return ret;
}

int frame_builder_wait(struct frame_builder_t* frame_builder, int min) {
    while (frame_builder->pending_jobs.size() > min ||
           frame_builder->finished_jobs.size() > min)
    {
        usleep(1000);
    }
    return 0;
}

static void free_job(struct frame_job_t* job) {
    for (struct frame_builder_subframe_t* subframe : job->subframes) {
        smart_frame_release(subframe->smart_frame);
    }
    job->subframes.clear();
    av_frame_free(&job->output_frame);
    free(job);
}

static void after_crunch_frame(uv_work_t* work, int status) {
    struct frame_job_t* job = (struct frame_job_t*)work->data;
    struct frame_builder_t* builder = job->builder;
    // move job from pending to finished, but don't call callback just yet...
    uv_mutex_lock(&builder->job_queue_lock);
    builder->pending_jobs.erase(job->serial_number);
    builder->finished_jobs[job->serial_number] = job;
    uv_mutex_unlock(&builder->job_queue_lock);

    // invoke callbacks and flush all finished jobs in order they were received.
    auto iter = builder->finished_jobs.find(builder->finish_serial);
    while (iter != builder->finished_jobs.end()) {
        iter->second->callback(iter->second->output_frame, iter->second->p);
        free_job(iter->second);
        builder->finished_jobs.erase(iter);
        builder->finish_serial++;
        iter = builder->finished_jobs.find(builder->finish_serial);
    }
}

static void crunch_frame(uv_work_t* work) {
    struct frame_job_t* job = (struct frame_job_t*)work->data;
    int ret;
    MagickWand* output_wand;
    magic_frame_start(&output_wand, job->width, job->height);

    // Configure output frame buffer
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = job->format;
    output_frame->width = job->width;
    output_frame->height = job->height;
    ret = av_frame_get_buffer(output_frame, 1);
    if (ret) {
        printf("No output AVFrame buffer to write video. Error: %s\n",
               av_err2str(ret));
    }

    if (!output_frame) {
        perror("Could not allocate frame");
    }
    
    for (struct frame_builder_subframe_t* subframe : job->subframes) {
        magic_frame_add(output_wand,
                        smart_frame_get(subframe->smart_frame),
                        subframe->x_offset,
                        subframe->y_offset,
                        subframe->render_width,
                        subframe->render_height,
                        subframe->scale_to_fit);
    }

    ret = magic_frame_finish(output_wand, output_frame, job->serial_number);

    job->output_frame = output_frame;

//    printf("Crunched %lu frames for frame builder job number %d\n",
//           job->subframes.size(), job->serial_number);
}

static void frame_builder_worker(void* p) {
    struct frame_builder_t* builder = (struct frame_builder_t*)p;
    int ret = 0;
    while (builder->running && 0 == ret) {
        ret = uv_run(builder->loop, UV_RUN_DEFAULT);
    }

    printf("goodbye loop!\n");
}

