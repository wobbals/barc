//
//  magic_frame.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

#ifndef magic_frame_h
#define magic_frame_h

#include <stdio.h>

#include <libavutil/frame.h>
#include <MagickWand/MagickWand.h>
#include <MagickWand/magick-image.h>
#include "object_fit.h"
#include "media_stream.h"

int magic_frame_start(MagickWand** dest_wand,
                      size_t width, size_t height);
int magic_frame_add(MagickWand* output_wand,
                    AVFrame* input_frame,
                    size_t x_offset,
                    size_t y_offset,
                    struct border_s border,
                    size_t output_width,
                    size_t output_height,
                    enum object_fit object_fit);
int magic_frame_finish(MagickWand* wand_out, AVFrame* frame_out,
                       int serial_number);

#endif /* magic_frame_h */
