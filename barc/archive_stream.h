//
//  archive_stream.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef archive_stream_h
#define archive_stream_h

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

struct archive_stream_t {
    int64_t start_offset;
    int64_t stop_offset;
    int64_t duration;
    AVFormatContext* format_context;
    AVCodecContext* decode_context;
    int video_stream_index;
    int audio_stream_index;
    AVFrame* current_frame;
    char current_frame_valid;
    char* sz_name;
    int source_width;
    int source_height;
    int x_offset;
    int y_offset;
    double scale_factor;
};

int archive_stream_open(struct archive_stream_t** stream_out,
                        const char *filename,
                        int64_t start_offset, int64_t stop_offset,
                        const char* stream_name);
int archive_stream_free(struct archive_stream_t* stream);

int archive_stream_peek_video_frame
(struct archive_stream_t* stream, AVFrame** frame, int64_t* offset_pts);

int archive_stream_pop_video_frame
(struct archive_stream_t* stream, AVFrame** frame, int64_t* offset_pts);

int archive_stream_is_active_at_time(struct archive_stream_t* stream,
                                     int64_t global_time);


#endif /* archive_stream_h */
