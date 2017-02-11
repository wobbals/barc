//
//  smart_avframe.c
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include "smart_avframe.h"
#include <uv.h>

struct smart_frame_t {
    AVFrame* frame;
    int ref;
    uv_mutex_t lock;
};

void smart_frame_create(struct smart_frame_t** smart_frame, AVFrame* frame) {
    struct smart_frame_t* result = (struct smart_frame_t*)
    calloc(1, sizeof(struct smart_frame_t));
    result->frame = frame;
    result->ref = 1;
    uv_mutex_init(&result->lock);
    *smart_frame = result;
}

void smart_frame_retain(struct smart_frame_t* frame) {
    uv_mutex_lock(&frame->lock);
    frame->ref++;
    uv_mutex_unlock(&frame->lock);
}

void smart_frame_release(struct smart_frame_t* frame) {
    char do_free = 0;
    uv_mutex_lock(&frame->lock);
    frame->ref--;
    if (frame->ref <= 0) {
        do_free = 1;
    }
    uv_mutex_unlock(&frame->lock);
    if (do_free) {
        av_frame_free(&frame->frame);
        uv_mutex_destroy(&frame->lock);
        free(frame);
    }
}

AVFrame* smart_frame_get(struct smart_frame_t* frame) {
    return frame->frame;
}
