//
//  audio_mixer.h
//  barc
//
//  Created by Charley Robinson on 2/1/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef audio_mixer_h
#define audio_mixer_h

#include <stdio.h>
#include <libavformat/avformat.h>

struct archive_t;
struct audio_mixer_t;

int audio_mixer_alloc(struct audio_mixer_t** mixer);
void audio_mixer_free(struct audio_mixer_t* mixer);

int audio_mixer_get_samples(struct archive_t* archive,
                            int64_t clock_time,
                            AVRational time_base,
                            AVFrame* output_frame);

#endif /* audio_mixer_h */
