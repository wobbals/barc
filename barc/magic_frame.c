//
//  magic_frame.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
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

// why is this not being found by magickwand include?
MagickBooleanType MagickSetImageMask(MagickWand *wand, const PixelMask type,
                                     const MagickWand *clip_mask);

int magic_frame_start(MagickWand** output_wand,
                      size_t out_width, size_t out_height)
{

    *output_wand = NewMagickWand();
    PixelWand* background = NewPixelWand();
    // set background for layout debugging
    //PixelSetGreen(background, 1.0);
    //PixelSetColor(background, "white");
    MagickNewImage(*output_wand, out_width, out_height, background);
    DestroyPixelWand(background);
    return 0;
}

static void draw_border_stroke(MagickWand* wand, size_t width, size_t height,
                               double thickness, double red, double green,
                               double blue)
{
  PixelWand* stroke_color = NewPixelWand();
  PixelSetRed(stroke_color, red / 255);
  PixelSetGreen(stroke_color, green / 255);
  PixelSetBlue(stroke_color, blue / 255);
  PixelWand* transparent_color = NewPixelWand();
  PixelSetColor(transparent_color, "white");
  MagickWand* mask = MagickGetImageMask(wand, ReadPixelMask);
  MagickWand* stroke_wand = NewMagickWand();
  MagickNewImage(stroke_wand, width, height, transparent_color);
  MagickAddImage(stroke_wand, mask);

  MagickScaleImage(wand, width - thickness, height - thickness);

  MagickSetImageMask(stroke_wand, ReadPixelMask, mask);
  MagickFloodfillPaintImage(stroke_wand, stroke_color, 150, stroke_color,
                            thickness/2, thickness/2, MagickTrue);
  MagickCompositeImage(stroke_wand, wand, OverCompositeOp, MagickTrue,
                       thickness / 2, thickness/2);
  MagickRemoveImage(wand);
  MagickAddImage(wand, stroke_wand);
  DestroyMagickWand(stroke_wand);
  DestroyPixelWand(transparent_color);
  DestroyPixelWand(stroke_color);
}

static void draw_border_radius(MagickWand* wand, int radius,
                               size_t width, size_t height)
{
  PixelWand* black_pixel = NewPixelWand();
  PixelSetColor(black_pixel, "#000000");
  PixelWand* white_pixel = NewPixelWand();
  PixelSetColor(white_pixel, "#ffffff");
  DrawingWand* rounded = NewDrawingWand();
  DrawSetFillColor(rounded, white_pixel);
  DrawRoundRectangle(rounded, 1, 1, width-1, height-1, radius, radius);

  MagickWand* border = NewMagickWand();
  MagickNewImage(border, width, height, black_pixel);
  MagickDrawImage(border, rounded);
  MagickSetImageMask(wand, ReadPixelMask, border);

  DestroyPixelWand(black_pixel);
  DestroyPixelWand(white_pixel);
  DestroyDrawingWand(rounded);
  DestroyMagickWand(border);
}

int magic_frame_add(MagickWand* output_wand,
                    AVFrame* input_frame,
                    size_t x_offset,
                    size_t y_offset,
                    struct border_s border,
                    size_t output_width,
                    size_t output_height,
                    enum object_fit object_fit)
{
    uint8_t* rgb_buf_in = malloc(RGB_BYTES_PER_PIXEL *
                                 input_frame->height * input_frame->width);

    // Convert colorspace (AVFrame YUV -> pixelbuf RGB)
    yuv420_rgb24_std(input_frame->width, input_frame->height,
                     input_frame->data[0],
                     input_frame->data[1],
                     input_frame->data[2],
                     input_frame->linesize[0],
                     input_frame->linesize[1],
                     rgb_buf_in,
                     input_frame->width * RGB_BYTES_PER_PIXEL,
                     YCBCR_709);

    // background on image color for scale/crop debugging
    PixelWand* background = NewPixelWand();
    // change the color to something with an active alpha channel if you need
    // to see what's happening with the image processor
    //PixelSetColor(background, "none");
    //PixelSetColor(background, "red");

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
    char rescale_dimensions = 0;

    // see https://developer.mozilla.org/en-US/docs/Web/CSS/object-fit
    if (object_fit_fill == object_fit) {
        // don't preserve aspect ratio.
        rescale_dimensions = 0;
    } else if (object_fit_scale_down == object_fit ||
               object_fit_contain == object_fit)
    {
        // scale to fit all source pixels inside container,
        // preserving aspect ratio
        scale_factor = fmin(w_factor, h_factor);
        rescale_dimensions = 1;
    } else {
        // fill the container completely, preserving aspect ratio
        scale_factor = fmax(w_factor, h_factor);
        rescale_dimensions = 1;
    }

    float scaled_width = output_width;
    float scaled_height = output_height;

    if (rescale_dimensions) {
        scaled_width = input_frame->width * scale_factor;
        scaled_height = input_frame->height * scale_factor;

        internal_y_offset = (scaled_height - output_height) / 2;
        internal_x_offset = (scaled_width - output_width) / 2;
    }

    MagickSetImageBackgroundColor(input_wand, background);
    MagickResizeImage(input_wand, scaled_width, scaled_height,
                      Lanczos2SharpFilter);
    // TODO: we should use extent without resizing for for object-fill: none.
    if (rescale_dimensions) {
        MagickExtentImage(input_wand, output_width, output_height,
                          internal_x_offset, internal_y_offset);
    }

  if (border.radius > 0) {
    draw_border_radius(input_wand, border.radius, output_width, output_height);
  }

  if (border.width > 0) {
    draw_border_stroke(input_wand, output_width, output_height,
                       border.width, border.red, border.green, border.blue);
  }

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
//    // debug individual frames
//    char buf[32];
//    sprintf(buf, "test-%d.png", serial_number);
//    MagickWriteImage(output_wand, buf);
//    exit(1);

    size_t width = MagickGetImageWidth(output_wand);
    size_t height = MagickGetImageHeight(output_wand);
    uint8_t* rgb_buf_out = malloc(RGB_BYTES_PER_PIXEL * width * height);

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
