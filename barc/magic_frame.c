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
    // set background for layout debugging
    //PixelSetGreen(background, 1.0);
    MagickNewImage(*output_wand, out_width, out_height, background);
    DestroyPixelWand(background);
    return 0;
}

int magic_frame_add(MagickWand* output_wand,
                    AVFrame* input_frame,
                    size_t x_offset,
                    size_t y_offset,
                    size_t output_width,
                    size_t output_height)
{
    uint8_t* rgb_buf_in = malloc(RGB_BYTES_PER_PIXEL *
                                 input_frame->height * input_frame->width);

    // Convert colorspace (AVFrame YUV -> pixelbuf RGB)
    yuv420_rgb24_std(input_frame->width, input_frame->height,
                     input_frame->data[0],
                     input_frame->data[1],
                     input_frame->data[2],
                     input_frame->linesize[0], input_frame->linesize[1],
                     rgb_buf_in, input_frame->width * 3,
                     YCBCR_709);

    // background on image color for scale/crop debugging
    PixelWand* background = NewPixelWand();
    //PixelSetRed(background, 1.0);

    MagickWand* input_wand = NewMagickWand();

    // import pixel buffer (there's gotta be a better way to do this)
    MagickBooleanType status = MagickConstituteImage(input_wand,
                                                     input_frame->width,
                                                     input_frame->height,
                                                     "RGB",
                                                     CharPixel,
                                                     rgb_buf_in);

    float w_factor = (float)output_width / (float)input_frame->width;
    float h_factor = (float)output_height / (float)input_frame->height;
    float scale_factor = 1;
    float internal_x_offset = 0;
    float internal_y_offset = 0;

    // TODO: make this switchable
#define FIT 1 // 0 for fill
    if (FIT) {
        scale_factor = fmin(w_factor, h_factor);
    } else {
        scale_factor = fmax(w_factor, h_factor);
    }

    float scaled_width = input_frame->width * scale_factor;
    float scaled_height = input_frame->height * scale_factor;

    internal_y_offset = (scaled_height - output_height) / 2;
    internal_x_offset = (scaled_width - output_width) / 2;

    MagickSetImageBackgroundColor(input_wand, background);
    MagickResizeImage(input_wand, scaled_width, scaled_height, CubicFilter);
    //MagickVignetteImage(input_wand, 0, 0, 0, 0);
    MagickExtentImage(input_wand, output_width, output_height,
                      internal_x_offset, internal_y_offset);

    if (status == MagickFalse)
        ThrowWandException(input_wand);

    // compose source frames
    MagickCompositeImage(output_wand, input_wand, OverCompositeOp,
                         MagickTrue, x_offset, y_offset);
    DestroyMagickWand(input_wand);
    DestroyPixelWand(background);

    free(rgb_buf_in);
    return 0;
}

int magic_frame_finish(MagickWand* output_wand, AVFrame* output_frame,
                       int serial_number)
{
    size_t width = MagickGetImageWidth(output_wand);
    size_t height = MagickGetImageHeight(output_wand);
    uint8_t* rgb_buf_out = malloc(RGB_BYTES_PER_PIXEL * width * height);
    memset(rgb_buf_out, 50, (RGB_BYTES_PER_PIXEL * width * height));

    // debug individual frames
//    char buf[32];
//    sprintf(buf, "test-%d.png", serial_number);
//    MagickWriteImage(output_wand, buf);

    // push modified wand back to rgb buffer
    MagickExportImagePixels(output_wand, 0, 0,
                            width,
                            height,
                            "RGB", CharPixel, rgb_buf_out);

    // send contrast_wand off to the frame buffer
    rgb24_yuv420_std(output_frame->width, output_frame->height,
                      rgb_buf_out, output_frame->width * RGB_BYTES_PER_PIXEL,
                      output_frame->data[0],
                      output_frame->data[1],
                      output_frame->data[2],
                      output_frame->linesize[0],
                      output_frame->linesize[1], YCBCR_709);

    free(rgb_buf_out);
    DestroyMagickWand(output_wand);
    return 0;
}
