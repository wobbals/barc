//
//  image_source.c
//  barc
//
//  Created by Charley Robinson on 5/10/17.
//

#include "image_source.h"
#include "source_container.h"
#include "media_stream.h"
#include <libavutil/frame.h>
#include <MagickWand/MagickWand.h>
#include <MagickWand/magick-image.h>
#include "yuv_rgb.h"

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
}

struct image_source_s {
  const char* sz_path;
  const char* sz_name;
  const char* sz_class;
  double start_offset;
  double stop_offset;
  AVFrame* frame;
  struct media_stream_s* media_stream;
  struct source_s* container;
};

int audio_callback(struct media_stream_s* stream,
                   AVFrame* frame, double time_clock,
                   void* p);
int video_callback(struct media_stream_s* stream,
                   struct smart_frame_t** frame_out,
                   double time_clock, void* p);
int read_image(struct image_source_s* pthis);
int is_active(void* p, double clock_time);
double get_stop_offset(void* p);
struct media_stream_s* get_media_stream(void* p);
void free_f(void* p);

int image_source_create(struct image_source_s** source_out, const char* path,
                        double start_offset, double stop_offset,
                        const char* stream_name,
                        const char* stream_class)
{
  struct image_source_s* pthis = calloc(1, sizeof(struct image_source_s));
  pthis->sz_path = path;
  pthis->start_offset = start_offset;
  pthis->stop_offset = stop_offset;
  pthis->sz_name = stream_name;
  pthis->sz_class = stream_class;

  int ret = media_stream_alloc(&pthis->media_stream);
  if (ret) {
    printf("image_source: unable to allocate media stream: %d\n", ret);
    free(pthis);
    return ret;
  }

  ret = source_create(&pthis->container,
                      is_active, get_stop_offset,
                      get_media_stream, free_f, pthis);
  if (ret) {
    printf("image_source: unable to allocate source container: %d\n", ret);
    free(pthis);
    return ret;
  }

  pthis->frame = av_frame_alloc();
  pthis->frame->format = AV_PIX_FMT_YUV420P;

  ret = read_image(pthis);
  if (ret) {
    free(pthis);
    return ret;
  }

  //set up media stream
  media_stream_set_name(pthis->media_stream, pthis->sz_name);
  media_stream_set_class(pthis->media_stream, pthis->sz_class);
  media_stream_set_audio_read(pthis->media_stream, audio_callback, pthis);
  media_stream_set_video_read(pthis->media_stream, video_callback, pthis);

  *source_out = pthis;
  return 0;
}

void image_source_free(struct image_source_s* pthis) {
  media_stream_free(pthis->media_stream);
  av_frame_free(&pthis->frame);
  free(pthis);
}

int read_image(struct image_source_s* pthis) {
  MagickWand* wand = NewMagickWand();
  MagickBooleanType ret = MagickReadImage(wand, pthis->sz_path);
  if (!ret) {
    printf("failed to read image at %s\n", pthis->sz_path);
    ThrowWandException(wand);
    DestroyMagickWand(wand);
    return -1;
  }
  
  size_t width = MagickGetImageWidth(wand);
  size_t height = MagickGetImageHeight(wand);

  pthis->frame->width = (int) width;
  pthis->frame->height = (int) height;
  int iret = av_frame_get_buffer(pthis->frame, 1);
  if (iret) {
    printf("unable to allocate image pixel buffer\n");
    DestroyMagickWand(wand);
    return iret;
  }

#define RGB_BYTES_PER_PIXEL 3
  uint8_t* pix = malloc(RGB_BYTES_PER_PIXEL * height * width);

  // export pixels to pthis->frame;
  ret = MagickExportImagePixels(wand, 0, 0, width, height, "RGB", CharPixel,
                                pix);

  // send contrast_wand off to the frame buffer
  rgb24_yuv420_std((int) width, (int)height,
                   pix, (int) width * RGB_BYTES_PER_PIXEL,
                   pthis->frame->data[0],
                   pthis->frame->data[1],
                   pthis->frame->data[2],
                   pthis->frame->linesize[0],
                   pthis->frame->linesize[1], YCBCR_709);
  free(pix);
  DestroyMagickWand(wand);
  return 0;
}

struct media_stream_s* image_source_get_stream(struct image_source_s* pthis)
{
  return pthis->media_stream;
}

struct source_s* image_source_get_container(struct image_source_s* pthis) {
  return pthis->container;
}

#pragma mark - Media callbacks

int audio_callback(struct media_stream_s* stream,
                   AVFrame* frame, double time_clock,
                   void* p)
{
  return -1;
}

int video_callback(struct media_stream_s* stream,
                   struct smart_frame_t** frame_out,
                   double time_clock, void* p)
{
  struct image_source_s* pthis = (struct image_source_s*)p;
  smart_frame_create(frame_out, pthis->frame);
  return 0;
}

#pragma mark - Container callbacks

int is_active(void* p, double clock_time) {
  struct image_source_s* pthis = (struct image_source_s*)p;
  return (pthis->start_offset <= clock_time &&
          clock_time < pthis->stop_offset);
}

double get_stop_offset(void* p) {
  struct image_source_s* pthis = (struct image_source_s*)p;
  return pthis->stop_offset;
}

struct media_stream_s* get_media_stream(void* p) {
  struct image_source_s* pthis = (struct image_source_s*)p;
  return pthis->media_stream;
}

void free_f(void* p) {
  struct image_source_s* pthis = (struct image_source_s*)p;
  image_source_free(pthis);
}
