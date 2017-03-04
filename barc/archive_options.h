//
//  archive_options.h
//  barc
//
//  Created by Charley Robinson on 2/22/17.
//

#ifndef archive_options_h
#define archive_options_h

enum image_mask {
    image_mask_none,
    image_mask_vignette
};

struct archive_options_s;
struct stream_options_s {
    enum image_mask image_mask;
};

int archive_options_load(struct archive_options_s** archive_options,
                               const char* path);
void archive_options_free(struct archive_options_s* archive_options);

int archive_options_get_stream_options
(struct archive_options_s* archive_options,
 const char* stream_id,
 struct stream_options_s* stream_options);

#endif /* archive_options_h */
