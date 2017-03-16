//
//  barc.h
//  barc
//
//  Created by Charley Robinson on 3/13/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef barc_h
#define barc_h

#include "media_stream.h"

struct barc_s;

struct barc_config_s {
  double video_framerate;
  size_t out_width;
  size_t out_height;
  const char* css_preset;
  const char* css_custom;
  const char* output_path;
};

struct barc_source_s {
  char* sz_name;
  char* sz_layout_class;
  struct media_stream_s* media_stream;
};

//alloc
//free
void barc_alloc(struct barc_s** barc_out);
void barc_free(struct barc_s* barc);
// set the outfile and media configuration
int barc_read_configuration(struct barc_s* barc, struct barc_config_s* config);
// open and prepare outfile for writing
int barc_open_outfile(struct barc_s* barc);
// finalize outfile when writing is complete
int barc_close_outfile(struct barc_s* barc);
//add media source
int barc_add_source(struct barc_s* barc, struct barc_source_s* source);
//remove stream
int barc_remove_source(struct barc_s* barc, struct barc_source_s* source);

int barc_tick(struct barc_s* barc);
double barc_get_current_clock(struct barc_s* barc);

#endif /* barc_h */
