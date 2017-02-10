//
//  frame_builder.c
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

extern "C" {

#include <stdlib.h>
#include "frame_builder.h"
#include <MagickWand/MagickWand.h>
#include "magic_frame.h"
}

#include <vector>

static int crunch(struct frame_job_t* job);
static void free_job(struct frame_job_t* job);

struct frame_job_t {
    int width;
    int height;
    enum AVPixelFormat format;
    std::vector<struct frame_builder_subframe_t*> subframes;
    frame_builder_cb_t callback;

    void* p;
};

struct frame_builder_t {
    struct frame_job_t* current_job;

};

int frame_builder_alloc(struct frame_builder_t** frame_builder) {
    struct frame_builder_t* result = (struct frame_builder_t*)
    calloc(1, sizeof(struct frame_builder_t));

    *frame_builder = result;
    return 0;
}

void frame_builder_free(struct frame_builder_t* frame_builder) {
    free(frame_builder);
}

int frame_builder_begin_frame(struct frame_builder_t* frame_builder,
                              int width, int height,
                              enum AVPixelFormat format, void* p)
{
    struct frame_job_t* job = (struct frame_job_t*)
    calloc(1, sizeof(struct frame_job_t));
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
    frame_builder->current_job->subframes.push_back(subframe_copy);
    return 0;
}

int frame_builder_finish_frame(struct frame_builder_t* frame_builder,
                               frame_builder_cb_t callback) {
    frame_builder->current_job->callback = callback;
    int ret = crunch(frame_builder->current_job);
    free_job(frame_builder->current_job);
    return ret;
}

static void free_job(struct frame_job_t* job) {
    job->subframes.clear();
    free(job);
}

static int crunch(struct frame_job_t* job) {
    int ret;
    MagickWand* output_wand;
    magic_frame_start(&output_wand, job->width, job->height);

    AVFrame* output_frame = av_frame_alloc();

    // Configure output frame buffer
    output_frame->format = job->format;
    output_frame->width = job->width;
    output_frame->height = job->height;
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
    
    for (struct frame_builder_subframe_t* subframe : job->subframes) {
        magic_frame_add(output_wand,
                        subframe->frame,
                        subframe->x_offset,
                        subframe->y_offset,
                        subframe->render_width,
                        subframe->render_height);
    }

    ret = magic_frame_finish(output_wand, output_frame);

    job->callback(output_frame, job->p);

    return ret;
}
