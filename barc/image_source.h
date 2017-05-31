//
//  image_source.h
//  barc
//
//  Created by Charley Robinson on 5/10/17.
//

#ifndef image_source_h
#define image_source_h

struct image_source_s;

/** Holds a single frame from an image file. */

int image_source_create(struct image_source_s** source_out, const char* path,
                        double start_offset, double stop_offset,
                        const char* stream_name,
                        const char* stream_class);
void image_source_free(struct image_source_s* image_source);
struct media_stream_s* image_source_get_stream(struct image_source_s* source);
struct source_s* image_source_get_container(struct image_source_s* source);
#endif /* image_source_h */
