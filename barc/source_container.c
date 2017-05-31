//
//  source_container.c
//  barc
//
//  Created by Charley Robinson on 5/11/17.
//

#include "source_container.h"
#include <stdlib.h>

struct source_s {
  is_active_cb* is_active;
  get_stop_offset_cb* get_stop_offset;
  get_media_stream_cb* get_media_stream;
  free_cb* free_f;
  void* parg;
};

int source_create(struct source_s** source_out,
                  is_active_cb* is_active,
                  get_stop_offset_cb* stop_offset,
                  get_media_stream_cb* get_media,
                  free_cb* free_f,
                  void* parg)
{
  struct source_s* pthis = calloc(1, sizeof(struct source_s));
  pthis->is_active = is_active;
  pthis->get_stop_offset = stop_offset;
  pthis->get_media_stream = get_media;
  pthis->parg = parg;
  pthis->free_f = free_f;
  *source_out = pthis;
  return 0;
}

void source_free(struct source_s* source) {
  source->free_f(source->parg);
  free(source);
}

int source_is_active_at_time(struct source_s* source, double clock_time) {
  return source->is_active(source->parg, clock_time);
}

double source_get_stop_offset(struct source_s* source) {
  return source->get_stop_offset(source->parg);
}

struct media_stream_s* source_get_media_stream(struct source_s* source) {
  return source->get_media_stream(source->parg);
}
