//
//  video_mixer.h
//  barc
//
//  Created by Charley Robinson on 3/14/17.
//

#ifndef video_mixer_h
#define video_mixer_h

#include "file_writer.h"
#include "media_stream.h"

struct video_mixer_s;

void video_mixer_alloc(struct video_mixer_s** mixer_out);
void video_mixer_free(struct video_mixer_s* mixer);
int video_mixer_add_stream(struct video_mixer_s* mixer,
                           struct media_stream_s* stream);
int video_mixer_remove_stream(struct video_mixer_s* mixer,
                              struct media_stream_s*);
int video_mixer_async_push_frame(struct video_mixer_s* mixer,
                                 struct file_writer_t* file_writer,
                                 double time_clock, int64_t pts);

void video_mixer_set_width(struct video_mixer_s* mixer, size_t width);
void video_mixer_set_height(struct video_mixer_s* mixer, size_t height);

void video_mixer_set_css_preset(struct video_mixer_s* mixer,
                                const char* preset);
void video_mixer_set_css_custom(struct video_mixer_s* mixer,
                                const char* css);

int video_mixer_flush(struct video_mixer_s* mixer);

#endif /* video_mixer_h */
