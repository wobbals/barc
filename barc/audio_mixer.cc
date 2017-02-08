//
//  audio_mixer.c
//  barc
//
//  Created by Charley Robinson on 2/1/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

extern "C" {
#include "audio_mixer.h"
#include "archive_stream.h"
#include "archive_package.h"
#include <libavutil/opt.h>
#include <assert.h>
}

#include <vector>

struct audio_mixer_t {

};

static int merge_audio_frame(AVFrame* dst_frame, AVFrame* src_frame,
                             int64_t dst_offset_num_samples,
                             int64_t* num_samples_copied)
{
    int ret;
    float output_sample;
    int16_t source_sample;
    int src_sample_size =
    av_get_bytes_per_sample((enum AVSampleFormat)src_frame->format);
    int64_t output_sample_idx = dst_offset_num_samples;
    assert(dst_frame->format == AV_SAMPLE_FMT_FLTP);
    assert(src_frame->format == AV_SAMPLE_FMT_S16);
    float* output_frame_samples = (float*)dst_frame->data[0];

    while(output_sample_idx < src_frame->nb_samples)
    {
        output_sample = 0;
        source_sample =
        *(src_frame->data[0]+(output_sample_idx * src_sample_size));
        output_sample += source_sample;
        output_sample /= INT16_MAX;
        if (fabs(output_sample) > 1.0) {
            // turn down for what
            printf("clip\n");
            output_sample = fmin(1.0, output_sample);
            output_sample = fmax(-1.0, output_sample);
        }

        // copy summed sample
        output_frame_samples[output_sample_idx] = output_sample;

        output_sample_idx++;
    }
    *num_samples_copied = output_sample_idx;
    return ret;
}


int audio_mixer_get_samples(struct archive_t* archive,
                            int64_t clock_time,
                            AVRational time_base,
                            AVFrame* output_frame)
{
    int ret;

    // get active streams
    // buffer contiguous data at least until we have enough to fill a frame
    // merge contiguous data
    // return

    struct archive_stream_t** active_streams;
    int active_stream_count;
    ret = archive_get_active_streams_for_time(archive, clock_time,
                                              &active_streams,
                                              &active_stream_count);
    printf("Will mix %d audio streams for ts %lld\n",
           active_stream_count, clock_time);

    if (active_stream_count <= 0) {
        return -1;
    }

    int16_t* source_samples[output_frame->channels];
    for (int i = 0; i < output_frame->channels; i++) {
        source_samples[i] =
        (int16_t*) calloc(sizeof(int16_t), output_frame->nb_samples);
    }

    ret = archive_stream_pop_audio_samples(active_streams[0],
                                           output_frame->nb_samples,
                                           AV_SAMPLE_FMT_S16,
                                           output_frame->sample_rate,
                                           source_samples);
    float* dest_samples = (float*)output_frame->data[0];
    for (int i = 0; i < output_frame->nb_samples; i++) {
        // TODO Make this dynamically typed
        dest_samples[i] = ((float)source_samples[0][i]) / INT16_MAX;
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
