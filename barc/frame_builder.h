//
//  frame_builder.h
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef frame_builder_h
#define frame_builder_h

#include <stdio.h>
#include <libavutil/frame.h>

struct frame_builder_t;

struct frame_builder_subframe_t {
    AVFrame* frame;
    int x_offset;
    int y_offset;
    int render_width;
    int render_height;
};

typedef void (*frame_builder_cb_t)(AVFrame* frame, void *p);

int frame_builder_alloc(struct frame_builder_t** frame_builder);
void frame_builder_free(struct frame_builder_t* frame_builder);

int frame_builder_begin_frame(struct frame_builder_t* frame_builder,
                              int width, int height,
                              enum AVPixelFormat, void* p);
int frame_builder_add_subframe(struct frame_builder_t* frame_builder,
                               struct frame_builder_subframe_t* subframe);
int frame_builder_finish_frame(struct frame_builder_t* frame_builder,
                               frame_builder_cb_t callback);
void frame_builder_release_frame();

#endif /* frame_builder_h */
