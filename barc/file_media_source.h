//
//  file_media_source.h
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef file_media_source_h
#define file_media_source_h

#include "archive_stream.h"

struct file_media_source_s;

int archive_stream_open_file(struct archive_stream_t** stream_out,
                             const char *filename,
                             int64_t start_offset, int64_t stop_offset,
                             const char* stream_name,
                             const char* stream_class);

int file_media_source_alloc(struct file_media_source_s** media_source_out);
void file_media_source_free(struct file_media_source_s*);

int file_stream_is_active_at_time(struct file_media_source_s* media_source,
                                     int64_t global_time,
                                     AVRational time_base);
int64_t file_stream_get_stop_offset(struct file_media_source_s* media_source);
int64_t file_stream_get_start_offset(struct file_media_source_s* media_source);

#endif /* file_media_source_h */
