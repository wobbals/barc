//
//  audio_mixer.h
//  barc
//
//  Created by Charley Robinson on 2/1/17.
//

#ifndef audio_mixer_h
#define audio_mixer_h

#include <stdio.h>
#include <libavformat/avformat.h>
#include "media_stream.h"

struct archive_s;
struct audio_mixer_t;

int audio_mixer_alloc(struct audio_mixer_t** mixer);
void audio_mixer_free(struct audio_mixer_t* mixer);

int audio_mixer_get_samples_for_streams
(struct media_stream_s** streams, size_t num_streams,
 double clock_time, AVFrame* output_frame);

#endif /* audio_mixer_h */
