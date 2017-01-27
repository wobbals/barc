//
//  magic_frame.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include "magic_frame.h"
#include "yuv_rgb.h"

#define RGB_BYTES_PER_PIXEL 3

#define ThrowWandException(wand) \
{ \
char \
*description; \
\
ExceptionType \
severity; \
\
description=MagickGetException(wand,&severity); \
(void) fprintf(stderr,"%s %s %lu %s\n",GetMagickModule(),description); \
description=(char *) MagickRelinquishMemory(description); \
exit(-1); \
}

int magic_frame_start(MagickWand** output_wand,
                      size_t out_width, size_t out_height)
{

    *output_wand = NewMagickWand();
    PixelWand* background = NewPixelWand();
    PixelSetGreen(background, 1.0);
    MagickNewImage(*output_wand, out_width, out_height, background);
    DestroyPixelWand(background);
    return 0;
}

int magic_frame_add(MagickWand* output_wand,
                    AVFrame* input_frame,
                    size_t x_offset,
                    size_t y_offset,
                    double scale_coeff)
{
    uint8_t* rgb_buf_in = malloc(RGB_BYTES_PER_PIXEL *
                                 input_frame->height * input_frame->width);

    // Convert colorspace (AVFrame YUV -> pixelbuf RGB)
    yuv420_rgb24_sseu(input_frame->width, input_frame->height,
                      input_frame->data[0],
                      input_frame->data[1],
                      input_frame->data[2],
                      input_frame->linesize[0], input_frame->linesize[1],
                      rgb_buf_in, input_frame->width * 3,
                      YCBCR_709);

    MagickWand* input_wand = NewMagickWand();

    // import pixel buffer (there's gotta be a better way to do this)
    MagickBooleanType status = MagickConstituteImage(input_wand,
                                                     input_frame->width,
                                                     input_frame->height,
                                                     "RGB",
                                                     CharPixel,
                                                     rgb_buf_in);

    size_t width = MagickGetImageWidth(input_wand);
    size_t height = MagickGetImageHeight(input_wand);
    MagickScaleImage(input_wand, scale_coeff * width, scale_coeff * height);

    if (status == MagickFalse)
        ThrowWandException(input_wand);

    // compose source frames
    MagickCompositeImage(output_wand, input_wand, OverCompositeOp,
                         MagickTrue, x_offset, y_offset);


    free(rgb_buf_in);
    return 0;
}

int magic_frame_finish(MagickWand* output_wand, AVFrame* output_frame)
{
    size_t width = MagickGetImageWidth(output_wand);
    size_t height = MagickGetImageHeight(output_wand);
    uint8_t* rgb_buf_out = malloc(RGB_BYTES_PER_PIXEL * width * height);

    // push modified wand back to rgb buffer
    MagickExportImagePixels(output_wand, 0, 0,
                            width,
                            height,
                            "RGB", CharPixel, rgb_buf_out);

    // send contrast_wand off to the frame buffer
    rgb24_yuv420_sseu(output_frame->width, output_frame->height,
                      rgb_buf_out, output_frame->width * RGB_BYTES_PER_PIXEL,
                      output_frame->data[0], output_frame->data[1], output_frame->data[2],
                      output_frame->linesize[0], output_frame->linesize[1], YCBCR_709);

    free(rgb_buf_out);
    output_wand=DestroyMagickWand(output_wand);
    return 0;
}
