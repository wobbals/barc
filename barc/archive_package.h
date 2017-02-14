//
//  archive_package.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef archive_package_h
#define archive_package_h

#include <stdio.h>
#include <libavutil/rational.h>

struct archive_t;

int archive_open(struct archive_t** archive_out, int width, int height,
                 const char* path,
                 const char* css_preset, const char* css_custom);
int archive_free(struct archive_t* archive);
int archive_populate_stream_coords(struct archive_t* archive,
                                   int64_t clock_time,
                                   AVRational clock_time_base);

int64_t archive_get_finish_clock_time(struct archive_t* archive);
int archive_get_active_streams_for_time(struct archive_t* archive,
                                        int64_t clock_time,
                                        AVRational time_base,
                                        struct archive_stream_t*** streams_out,
                                        int* num_streams_out);

void archive_set_output_video_fps(struct archive_t* archive, int fps);

#endif /* archive_package_h */
