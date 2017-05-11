//
//  source_container.h
//  barc
//
//  Created by Charley Robinson on 5/11/17.
//

#ifndef source_container_h
#define source_container_h

struct media_stream_s;
struct source_s;

typedef int (is_active_cb)(void* pthis, double clock_time);
typedef double (get_stop_offset_cb)(void* pthis);
typedef struct media_stream_s* (get_media_stream_cb)(void* pthis);
typedef void (free_cb)(void* pthis);

int source_create(struct source_s** source_out,
                  is_active_cb* is_active,
                  get_stop_offset_cb* stop_offset,
                  get_media_stream_cb* get_media,
                  free_cb* free_f,
                  void* parg);
void source_free(struct source_s* source);

int source_is_active_at_time(struct source_s* source, double clock_time);
double source_get_stop_offset(struct source_s* source);
struct media_stream_s* source_get_media_stream(struct source_s* source);

#endif /* source_container_h */
