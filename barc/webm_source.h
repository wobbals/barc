//
//  webm_source.h
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//

#ifndef webm_source_h
#define webm_source_h

#include "media_stream.h"

struct webm_source_s;

int webm_source_open(struct webm_source_s** source_out,
                           const char *filename,
                           double start_offset, double stop_offset,
                           const char* stream_name,
                           const char* stream_class);
void webm_source_free(struct webm_source_s*);
int webm_source_seek(struct webm_source_s* media_source,
                           double to_time);
int file_stream_is_active_at_time(struct webm_source_s* media_source,
                                  double clock_time);
double file_stream_get_stop_offset(struct webm_source_s* media_source);
struct media_stream_s* webm_source_get_stream
(struct webm_source_s* source);
struct source_s* webm_source_get_container(struct webm_source_s* p);
#endif /* webm_source_h */
