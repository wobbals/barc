//
//  archive_stream.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

#ifndef archive_stream_h
#define archive_stream_h

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "smart_avframe.h"
#include "object_fit.h"

struct archive_stream_t;

int archive_stream_open(struct archive_stream_t** stream_out,
                        const char *filename,
                        int64_t start_offset, int64_t stop_offset,
                        const char* stream_name,
                        const char* stream_class);
int archive_stream_free(struct archive_stream_t* stream);

void archive_stream_set_output_video_fps(struct archive_stream_t* stream,
                                         int fps);

int archive_stream_get_video_for_time(struct archive_stream_t* stream,
                                      struct smart_frame_t** frame,
                                      int64_t clock_time,
                                      AVRational clock_time_base);

int archive_stream_has_video_for_time(struct archive_stream_t* stream,
                                      int64_t clock_time,
                                      AVRational clock_time_base);

/**
 * @return The number of samples read into *samples_out
 */
int archive_stream_pop_audio_samples(struct archive_stream_t* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out,
                                     int64_t clock_time,
                                     AVRational time_base);

int archive_stream_is_active_at_time(struct archive_stream_t* stream,
                                     int64_t global_time,
                                     AVRational time_base);


int archive_stream_get_offset_x(struct archive_stream_t* stream);
int archive_stream_get_offset_y(struct archive_stream_t* stream);
int archive_stream_get_render_width(struct archive_stream_t* stream);
int archive_stream_get_render_height(struct archive_stream_t* stream);
enum object_fit archive_stream_get_object_fit(struct archive_stream_t* stream);
void archive_stream_set_offset_x(struct archive_stream_t* stream,
                                 int x_offset);
void archive_stream_set_offset_y(struct archive_stream_t* stream,
                                 int y_offset);
void archive_stream_set_render_width(struct archive_stream_t* stream,
                                     int width);
void archive_stream_set_render_height(struct archive_stream_t* stream,
                                      int height);
void archive_stream_set_object_fit(struct archive_stream_t* stream,
                                   enum object_fit object_fit);
int64_t archive_stream_get_stop_offset(struct archive_stream_t* stream);
int64_t archive_stream_get_start_offset(struct archive_stream_t* stream);
const char* archive_stream_get_name(struct archive_stream_t* stream);
const char* archive_stream_get_class(struct archive_stream_t* stream);

#endif /* archive_stream_h */
