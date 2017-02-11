//
//  smart_avframe.h
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef smart_avframe_h
#define smart_avframe_h

#include <libavutil/frame.h>

struct smart_frame_t;

/* Wraps an AVFrame in a thread safe reference counter.
 * new instances have a reference count of 1.
 */
void smart_frame_create(struct smart_frame_t** smart_frame, AVFrame* frame);
/* Increment reference count
 */
void smart_frame_retain(struct smart_frame_t* frame);
/* Decrement reference count. Once reference count hits zero, the AVFrame that
 * created this container will be invoked with av_frame_free(AVFrame**),
 * and the container itself will also be freed.
 */
void smart_frame_release(struct smart_frame_t* frame);

AVFrame* smart_frame_get(struct smart_frame_t* frame);

#endif /* smart_avframe_h */
