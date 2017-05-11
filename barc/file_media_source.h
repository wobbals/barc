//
//  file_media_source.h
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//

#ifndef file_media_source_h
#define file_media_source_h

#include "media_stream.h"

struct file_media_source_s;

int file_media_source_open(struct file_media_source_s** source_out,
                           const char *filename,
                           double start_offset, double stop_offset,
                           const char* stream_name,
                           const char* stream_class);
void file_media_source_free(struct file_media_source_s*);
int file_media_source_seek(struct file_media_source_s* media_source,
                           double to_time);
int file_stream_is_active_at_time(struct file_media_source_s* media_source,
                                  double clock_time);
double file_stream_get_stop_offset(struct file_media_source_s* media_source);
struct media_stream_s* file_media_source_get_stream
(struct file_media_source_s* source);
struct source_s* file_media_source_get_container(struct file_media_source_s* p);
#endif /* file_media_source_h */
