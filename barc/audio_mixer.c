//
//  audio_mixer.c
//  barc
//
//  Created by Charley Robinson on 2/1/17.
//

#include "audio_mixer.h"
#include "archive_package.h"
#include <libavutil/opt.h>
#include <assert.h>

struct audio_mixer_t {

};

int audio_mixer_get_samples_for_streams
(struct media_stream_s** active_streams, size_t active_stream_count,
 double clock_time, AVFrame* output_frame)
{
  int ret = 0;
    printf("Will mix %zu audio streams for ts %f\n",
           active_stream_count, clock_time);

    if (active_stream_count <= 0) {
        return -1;
    }

    int16_t* source_samples[output_frame->channels];
    for (int i = 0; i < output_frame->channels; i++) {
        source_samples[i] =
        (int16_t*) calloc(sizeof(int16_t), output_frame->nb_samples);
    }
    assert(output_frame->format == AV_SAMPLE_FMT_FLTP);
    float** dest_samples = (float**)output_frame->data;

    // third loop for each active stream
    for (int i = 0; i < active_stream_count; i++) {
        ret = archive_stream_get_audio_samples(active_streams[i],
                                               output_frame->nb_samples,
                                               AV_SAMPLE_FMT_S16,
                                               output_frame->sample_rate,
                                               source_samples,
                                               output_frame->channels,
                                               clock_time);
        // don't copy in samples we haven't read
        if (ret != output_frame->nb_samples) {
            continue;
        }
        // second loop for each channel
        for (int j = 0; j < output_frame->channels; j++) {
            // first loop to copy samples for a channel
            for (int k = 0; k < output_frame->nb_samples; k++) {
                // TODO Make this dynamically typed
                dest_samples[j][k] +=
                (((float)source_samples[j][k]) / INT16_MAX);
                if (fabs(dest_samples[j][k]) > 1.0) {
                    // turn down for what
                    printf("clip\n");
                    dest_samples[j][k] = fmin(1.0, dest_samples[j][k]);
                    dest_samples[j][k] = fmax(-1.0, dest_samples[j][k]);
                }
            }
        }
    }

    for (int i = 0; i < output_frame->channels; i++) {
        free(source_samples[i]);
    }

    return ret;
}

int audio_mixer_alloc(struct audio_mixer_t** mixer) {
    return 0;
}

void audio_mixer_free(struct audio_mixer_t* mixer) {

}
